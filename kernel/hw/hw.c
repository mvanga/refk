#include "klib/kutil.h"
#include "klib/kcomm.h"

#include "../scheduler/interface.h"

#include "acpica/platform/acenv.h"
#include "acpica/acpi.h"

#include "osl.h"
#include "pci.h"
#include "ioapic.h"

#include "rlib/global.h"
#include "rlib/heap.h"

char *clone_string(const char *orig, uint64_t length) {
    char *ret = malloc(length+1);
    memcpy(ret, orig, length);
    ret[length] = 0;
    return ret;
}

static void process_pci_device(ACPI_HANDLE object) {
    ACPI_DEVICE_INFO *info;

    if(AcpiGetObjectInfo(object, &info) != AE_OK) {
        return;
    }
    uint64_t function = info->Address & 0xffff;
    uint64_t slot = info->Address >> 16;
    // get PCI type

    d_printf("    PCI address: bus 0 slot %x function %x\n", slot, function);
    uint32_t type = pci_readconfig(0, slot, function, 0);
    d_printf("    PCI type: %x\n", type);
    uint16_t vid = type & 0xffff;
    uint16_t pid = type >> 16;

    if(vid == 0x10ec && pid == 0x8139) {
        d_printf("    is RTL8139!\n");
    }
}

static ACPI_STATUS device_callback(ACPI_HANDLE object, UINT32 nesting,
    void *context, void **ret) {

    d_printf("device %x:\n", object);
    //d_printf("    nesting: %x\n", nesting);

    ACPI_BUFFER buf;
    buf.Length = ACPI_ALLOCATE_BUFFER;

    AcpiGetName(object, ACPI_FULL_PATHNAME, &buf);
    d_printf("    full name: %s\n", buf.Pointer);
    free(buf.Pointer);

    ACPI_DEVICE_INFO *info;

    if(AcpiGetObjectInfo(object, &info) != AE_OK) {
        d_printf("    no _HID!\n");
        return AE_OK;
    }
    
    if(info->Valid & ACPI_VALID_HID) {
        char *cloned = clone_string(info->HardwareId.String, info->HardwareId.Length);
        d_printf("    hardware ID: %s\n", cloned);
        free(cloned);
    }
    if(info->Valid & ACPI_VALID_CID) {
        for(uint64_t i = 0; i < info->CompatibleIdList.Count; i ++) {
            char *cloned = clone_string(info->CompatibleIdList.Ids[i].String,
                info->CompatibleIdList.Ids[i].Length);
            d_printf("    compatible hardware ID: %s\n", cloned);
            free(cloned);
        }
    }

    d_printf("    class code length: %x\n", info->ClassCode.Length);

    if(info->HardwareId.Length &&
        !strncmp(info->HardwareId.String, "PNP0A08", info->HardwareId.Length)) {

        d_printf("        found PCI root!\n");

        d_printf("Getting routing table...\n");
        ACPI_BUFFER retbuf;
        retbuf.Length = ACPI_ALLOCATE_BUFFER;

        ACPI_STATUS ret = AcpiGetIrqRoutingTable(object, &retbuf);
        if(ret == AE_OK) {
            d_printf("        Routing table size: %x\n", retbuf.Length);
            uint64_t offset = 0;
            while(offset+sizeof(ACPI_PCI_ROUTING_TABLE) <= retbuf.Length) {
                ACPI_PCI_ROUTING_TABLE *routing = (void *)((uint8_t *)retbuf.Pointer + offset);
                if(routing->Length == 0) break;
                d_printf("            route %x's %x to %x (offset %x)\n",
                    routing->Address, routing->SourceIndex, routing->Pin,
                    offset);

                offset += routing->Length;
            }
        }
        else d_printf("        Routing table get failed: %x\n", ret);
    }

    if(info->Valid & ACPI_VALID_ADR) {
        d_printf("    address: %x\n", info->Address);
    }

    return AE_OK;
}

/*
    General outline:
    - parse ACPI tables
    - set up ACPICA
    - begin answering queries
*/

void find_apics() {
    // get the APIC table
    ACPI_TABLE_HEADER *madt_table_header;
    ACPI_STATUS ret = AcpiGetTable(ACPI_SIG_MADT, 1, &madt_table_header);
    if(ret != AE_OK) {
        d_printf("Failed to find APIC table!\n");
        while(1) {}
    }
    ACPI_TABLE_MADT *madt_table = (void *)madt_table_header;
    if(madt_table->Flags & ACPI_MADT_PCAT_COMPAT) {
        /* Disable 8259 PIC emulation. */
        /* Set ICW1 */
        koutb(0x20, 0x11);
        koutb(0xa0, 0x11);

        /* Set ICW2 (IRQ base offsets) */
        koutb(0x21, 0xe0);
        koutb(0xa1, 0xe8);

        /* Set ICW3 */
        koutb(0x21, 4);
        koutb(0xa1, 2);

        /* Set ICW4 */
        koutb(0x21, 1);
        koutb(0xa1, 1);

        /* Set OCW1 (interrupt masks) */
        koutb(0x21, 0xff);
        koutb(0xa1, 0xff);
    }

    uint8_t *begin = (void *)(madt_table + 1);
    uint64_t offset = 0;
    while(offset + sizeof(*madt_table) < madt_table->Header.Length) {
        ACPI_SUBTABLE_HEADER *sheader = (void *)(begin + offset);

        //d_printf("Subtable header length: %x type: %x\n", sheader->Length, sheader->Type);
        if(sheader->Type == ACPI_MADT_TYPE_IO_APIC) {
            ACPI_MADT_IO_APIC *ioapic = (void *)sheader;
            d_printf("Found I/O APIC! address: %x base: %x\n", ioapic->Address, ioapic->GlobalIrqBase);
        }
        else if(sheader->Type == ACPI_MADT_TYPE_LOCAL_APIC) {
            ACPI_MADT_LOCAL_APIC *lapic = (void *)sheader;
            d_printf("Found local APIC! procid: %x flags: %x\n", lapic->ProcessorId, lapic->LapicFlags);
        }
        else if(sheader->Type == ACPI_MADT_TYPE_INTERRUPT_OVERRIDE) {
            ACPI_MADT_INTERRUPT_OVERRIDE *iover = (void *)sheader;
            d_printf("Found interrupt override! ISA interrupt %x should be %x\n", iover->SourceIrq, iover->GlobalIrq);
        }
        else if(sheader->Type == ACPI_MADT_TYPE_LOCAL_APIC_NMI) {
            ACPI_MADT_LOCAL_APIC_NMI *nmi = (void *)sheader;
            d_printf("Found local APIC NMI! processor %x has NMI connected to %x\n", nmi->ProcessorId, nmi->Lint);
        }
        offset += sheader->Length;
    }
}

void _start() {
    rlib_setup(RLIB_DEFAULT_HEAP);

    uint64_t own_id;
    kcomm_t *schedin, *schedout;
    __asm__ __volatile__("mov %%gs:0x00, %%rax" : "=a"(own_id));
    __asm__ __volatile__("mov %%gs:0x08, %%rax" : "=a"(schedin));
    __asm__ __volatile__("mov %%gs:0x10, %%rax" : "=a"(schedout));


    ACPI_TABLE_DESC tables[32];
    ACPI_STATUS ret = AcpiInitializeTables(tables, 32, 0);

    if(ret != AE_OK) {
        d_printf("Failed to read ACPI tables!\n");
        while(1) {}
    }

    find_apics();

    AcpiInitializeSubsystem();
    AcpiLoadTables();
    AcpiEnableSubsystem(ACPI_FULL_INITIALIZATION);
    AcpiInitializeObjects(ACPI_FULL_INITIALIZATION);

    // tell ACPI we're using the I/O APICs
    {
        ACPI_OBJECT mode;
        mode.Type = ACPI_TYPE_INTEGER;
        mode.Integer.Value = 1; // I/O APICs
        ACPI_OBJECT_LIST list;
        list.Pointer = &mode;
        list.Count = 1;
        ACPI_STATUS ret = AcpiEvaluateObject(0, "\\_PIC", &list, 0);
        if(ret != AE_OK) d_printf("Failed to invoke \\_PIC: %x\n", ret);
        else d_printf("Successfully told ACPI to use I/O APIC mode!\n");
    }

    pci_probe_all();

    {
        void *r;
        uint64_t ret = AcpiGetDevices(0, &device_callback, 0, &r);
        d_printf("get_devices ret: %x\n", ret);
    }

    d_printf("initialized\n");
    while(1) {}
}
