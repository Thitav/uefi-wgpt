#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

typedef struct
{
  uint8_t boot_indicator;
  uint8_t starting_chs[3];
  uint8_t os_type;
  uint8_t ending_chs[3];
  uint32_t starting_lba;
  uint32_t size_in_lba;
} __attribute__((packed)) MBR_Partition_Record;

typedef struct
{
  uint8_t boot_code[440];
  uint32_t disk_signature;
  uint16_t unknown;
  MBR_Partition_Record partition_record[4];
  uint16_t signature;
} __attribute__((packed)) MBR;

char *image_name = "test.img";

uint64_t bytes_to_lba(uint64_t bytes, uint64_t block_size)
{
  return (bytes + block_size - 1) / block_size;
}

bool write_pmbr(FILE *image)
{
  MBR pmbr = {
      .boot_code = {0},
      .disk_signature = 0,
      .unknown = 0,
      .partition_record = {
          {
              .boot_indicator = 0x00,
              .starting_chs = {0x00, 0x02, 0x00},
              .os_type = 0xEE,
              .ending_chs = {0xFF, 0xFF, 0xFF}, // TODO
              .starting_lba = 0x00000001,
              .size_in_lba = 0xFFFFFFFF, // TODO
          },
      },
      .signature = 0xAA55,
  };

  if (fwrite(&pmbr, 1, sizeof(pmbr), image) != sizeof(pmbr))
  {
    return false;
  }
  return true;
}

int main()
{
  FILE *image = fopen(image_name, "wb");
  if (!image)
  {
    fprintf(stderr, "Error: could not open file %s\n", image_name);
    return EXIT_FAILURE;
  }

  // Logical block size
  uint64_t block_size = 512; // 512 bytes
  // EFI system partition size
  uint64_t esp_size = 1024 * 1024 * 33; // 33MB
  // Data partition size
  uint64_t data_size = 1024 * 1024 * 1; // 1MB
  // Padding size
  uint64_t padding_size = 1024 * 1024 * 1; // 1MB

  uint64_t image_size = esp_size + data_size + padding_size;
  uint64_t image_size_in_lba = bytes_to_lba(image_size, block_size);

  if (!write_pmbr(image))
  {
    fprintf(stderr, "Error: could not write protective MBR for file %s\n", image_name);
    return EXIT_FAILURE;
  }

  return EXIT_SUCCESS;
}
