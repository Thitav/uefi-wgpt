#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

typedef struct
{
  uint32_t time_lo;
  uint16_t time_mid;
  uint16_t time_hi_and_ver;     // Highest 4 bits are version
  uint8_t clock_seq_hi_and_res; // Highest bits are variant
  uint8_t clock_seq_lo;
  uint8_t node[6];
} __attribute__((packed)) GUID;

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

typedef struct
{
  uint64_t signature;
  uint32_t revision;
  uint32_t header_size;
  uint32_t header_crc32;
  uint32_t reserved;
  uint64_t my_lba;
  uint64_t alternate_lba;
  uint64_t first_usable_lba;
  uint64_t last_usable_lba;
  GUID disk_guid;
  uint64_t partition_entry_lba;
  uint32_t number_of_partition_entries;
  uint32_t size_of_partition_entry;
  uint32_t partition_entry_array_crc32;
} __attribute__((packed)) GPT_Header;

typedef struct
{
  char *filename;
  FILE *fp;
  uint64_t block_size;
  uint64_t size_in_bytes;
  uint64_t size_in_lba;
} Image;

uint64_t bytes_to_lba(uint64_t bytes, uint64_t block_size)
{
  return (bytes + block_size - 1) / block_size;
}

// version 4 variant 2 GUID
GUID generate_guid(void)
{
  uint8_t rand_bytes[16];

  for (uint8_t i = 0; i < sizeof(rand_bytes); i++)
  {
    rand_bytes[i] = rand() & 0xFF; // rand() % (UINT8_MAX + 1)
  }

  // Fill out GUID
  GUID guid = {
      .time_lo = *(uint32_t *)&rand_bytes[0],
      .time_mid = *(uint16_t *)&rand_bytes[4],
      .time_hi_and_ver = *(uint16_t *)&rand_bytes[6],
      .clock_seq_hi_and_res = rand_bytes[8],
      .clock_seq_lo = rand_bytes[9],
      .node = {rand_bytes[10], rand_bytes[11], rand_bytes[12], rand_bytes[13],
               rand_bytes[14], rand_bytes[15]},
  };

  // Version bits (version 4)
  guid.time_hi_and_ver &= ~(1 << 15); // 0b_0_111 1111
  guid.time_hi_and_ver |= (1 << 14);  // 0b0_1_00 0000
  guid.time_hi_and_ver &= ~(1 << 13); // 0b11_0_1 1111
  guid.time_hi_and_ver &= ~(1 << 12); // 0b111_0_ 1111

  // Variant bits
  guid.clock_seq_hi_and_res |= (1 << 7);  // 0b_1_000 0000
  guid.clock_seq_hi_and_res |= (1 << 6);  // 0b0_1_00 0000
  guid.clock_seq_hi_and_res &= ~(1 << 5); // 0b11_0_1 1111

  return guid;
}

bool image_write_pmbr(Image *image)
{
  // Protective MBR
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
              .size_in_lba = (uint32_t)(image->size_in_lba > UINT32_MAX ? UINT32_MAX : image->size_in_lba - 1), // TODO
          },
      },
      .signature = 0xAA55,
  };

  if (fwrite(&pmbr, sizeof(char), sizeof(pmbr), image->fp) != sizeof(pmbr))
  {
    return false;
  }
  return true;
}

bool image_write_gpt(Image *image)
{
  GPT_Header gpt = {
      .signature = 0x5452415020494645, // "EFI PART"
      .revision = 0x00010000,
      .header_size = 92,
      .header_crc32 = 0, // TODO: calculate
      .reserved = 0,
      .my_lba = 0x00000001,
      .alternate_lba = image->size_in_lba - 1,
      .first_usable_lba = bytes_to_lba(16 * 1024, image->block_size) + 2, // 16KB + PMBR + GPT
      .last_usable_lba = image->size_in_lba - 2,
      .disk_guid = generate_guid(), // TODO: generate
      .partition_entry_lba = 0x00000002,
      .number_of_partition_entries = 0x00000000, // TODO
      .size_of_partition_entry = 128 * 2 * 1,    // TODO: replace '1' with variable
      .partition_entry_array_crc32 = 0x00000000,
  };

  // TODO: Calculate crc32s

  if (fwrite(&gpt, sizeof(char), sizeof(gpt), image->fp) != sizeof(gpt))
  {
    return false;
  }

  uint64_t tmp = gpt.my_lba;
  gpt.my_lba = gpt.alternate_lba;
  gpt.alternate_lba = tmp;

  if (fseek(image->fp, image->size_in_bytes - image->block_size, SEEK_SET) != 0)
  {
    return false;
  }
  if (fwrite(&gpt, sizeof(char), sizeof(gpt), image->fp) != sizeof(gpt))
  {
    return false;
  }

  return true;
}

int main()
{
  Image image;

  image.filename = "test.img";
  image.fp = fopen(image.filename, "wb");
  if (!image.fp)
  {
    fprintf(stderr, "Error: could not open file %s\n", image.filename);
    return EXIT_FAILURE;
  }

  // EFI system partition size
  uint64_t esp_size = 1024 * 1024 * 33; // 33MB
  // Data partition size
  uint64_t data_size = 1024 * 1024 * 1; // 1MB
  // Padding size
  uint64_t padding_size = 1024 * 1024 * 1; // 1MB

  image.block_size = 512;
  image.size_in_bytes = esp_size + data_size + padding_size; // 35MB
  image.size_in_lba = bytes_to_lba(image.size_in_bytes, image.block_size);

  if (!image_write_pmbr(&image))
  {
    fprintf(stderr, "Error: could not write protective MBR for file %s\n", image.filename);
    return EXIT_FAILURE;
  }

  if (!image_write_gpt(&image))
  {
    fprintf(stderr, "Error: could not write GPT for file %s\n", image.filename);
    return EXIT_FAILURE;
  }

  return EXIT_SUCCESS;
}
