typedef struct global global_t;
#define usb_callback_data_t global_t

#include <usbdrvce.h>
#include <fatdrvce.h>
#include <tice.h>

#include <stdio.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#define ROM_DUMP_SIZE (1024 * 1024 * 4)
#define MAX_PARTITIONS 10
#define ROM_DUMP_FILE "ROMDUMP.ROM"
#define ROM_DUMP_PATH "/"
#define ROM_DUMP_NAME ROM_DUMP_PATH ROM_DUMP_FILE
#define ROM_BUFFER_SIZE (MSD_SECTOR_SIZE * 64)

struct global
{
    usb_device_t usb;
    msd_device_t msd;
};

static uint8_t rombuffer[ROM_BUFFER_SIZE];

static void putstr(char *str)
{
    os_PutStrFull(str);
    os_NewLine();
}

static usb_error_t handleUsbEvent(usb_event_t event, void *event_data,
                                  usb_callback_data_t *global)
{
    switch (event)
    {
        case USB_DEVICE_DISCONNECTED_EVENT:
            // close the usb device the msd was using
            msd_Close(&global->msd);
            global->usb = NULL;
            putstr("usb device disconnected");
            break;
        case USB_DEVICE_CONNECTED_EVENT:
            putstr("usb device connected");
            return usb_ResetDevice(event_data);
        case USB_DEVICE_ENABLED_EVENT:
            global->usb = event_data;
            putstr("usb device enabled");
            break;
        default:
            break;
    }

    return USB_SUCCESS;
}

int main(void)
{
    static uint8_t msd_buffer[MSD_SECTOR_SIZE];
    static fat_partition_t fatparts[MAX_PARTITIONS];
    static global_t global;
    static fat_t fat;
    uint8_t numparts;
    bool valid;
    usb_error_t usberr;
    msd_error_t msderr;
    fat_error_t faterr;

    memset(&global, 0, sizeof(global_t));
    valid = false;

    os_SetCursorPos(1, 0);

    usberr = usb_Init(handleUsbEvent, &global, NULL, USB_DEFAULT_INIT_FLAGS);
    if (usberr != USB_SUCCESS)
    {
        putstr("usb init error.");
        os_GetKey();
        return -1;
    }

    do
    {
        // check for any usb events
        usberr = usb_WaitForInterrupt();
        if (usberr != USB_SUCCESS)
        {
            putstr("usb library error.");
            os_GetKey();
            return -1;
        }

        // if a device is plugged in, initialize it
        if (!valid && global.usb != NULL)
        {
            // initialize the msd device
            msderr = msd_Open(&global.msd, global.usb, msd_buffer);
            if (msderr != MSD_SUCCESS)
            {
                putstr("failed opening msd");
                usb_Cleanup();
                os_GetKey();
                return -1;
            }

            putstr("opened msd");
            valid = true;
            break;
        }
    } while (!os_GetCSC());

    // locate any fat partitions on the drive
    faterr = fat_FindPartitions(&global.msd, fatparts, &numparts, MAX_PARTITIONS);
    if (faterr != FAT_SUCCESS)
    {
        putstr("error finding fat partitions");
        return -1;
    }

    // verify there is at least one fat parition
    if (numparts == 0)
    {
        putstr("no fat paritions on device");
        return -1;
    }

    // attempt fat init on first fat partition
    faterr = fat_OpenPartition(&fat, &fatparts[0]);
    if (faterr != FAT_SUCCESS)
    {
        putstr("could not open fat partition");
        return -1;
    }

    // attempt to create a file
    if (faterr == FAT_SUCCESS)
    {
        fat_file_t *file;
        uintptr_t i;

        putstr("creating dump file...");

        // create the rom dump file, deleting it if it exists first
        fat_Delete(&fat, ROM_DUMP_NAME);
        fat_Create(&fat, ROM_DUMP_PATH, ROM_DUMP_FILE, FAT_FILE);

        // set the size of the rom dump
        fat_SetSize(&fat, ROM_DUMP_NAME, ROM_DUMP_SIZE);

        putstr("writing dump file...");

        // open dump file for writing
        file = fat_Open(&fat, ROM_DUMP_NAME, FAT_WRONLY);

        // write the rom file, starting at the memory base address
        // dma only works from ram, so copy to a temporary buffer
        for (i = 0; i < ROM_DUMP_SIZE; i += ROM_BUFFER_SIZE)
        {
            memcpy(rombuffer, (const void *)i, ROM_BUFFER_SIZE);
            fat_WriteSectors(file, ROM_BUFFER_SIZE / MSD_SECTOR_SIZE, rombuffer);
        }

        // close the file
        faterr = fat_Close(file);
        if (faterr == USB_SUCCESS)
            putstr("dumped rom!");

        // close the partition
        fat_ClosePartition(&fat);

        // close the msd device
        msd_Close(&global.msd);
    }

    // cleanup and return
    usb_Cleanup();
    os_GetKey();
}