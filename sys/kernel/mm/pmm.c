/*
 *  description: Physical memory manager.
 *  author: Ian Marco Moffett.
 */

#include <mm/pmm.h>
#include <mm/vmm.h>
#include <sys/limine.h>
#include <sys/module.h>
#include <sys/printk.h>
#include <sys/math.h>
#include <sys/panic.h>
#include <sys/bitmap.h>
#include <string.h>

MODULE("pmm");

static volatile struct limine_memmap_request mmap_req = {
  .id = LIMINE_MEMMAP_REQUEST,
  .revision = 0
};

static struct limine_memmap_response *mmap_resp = NULL;
static bitmap_t bitmap = NULL;

static struct limine_memmap_entry *find_highest_mmap_entry(void)
{
  struct limine_memmap_entry *entry = NULL;

  for (size_t i = 0; i < mmap_resp->entry_count; ++i)
  {
    struct limine_memmap_entry* current_entry = mmap_resp->entries[i];
    if (current_entry->type != LIMINE_MEMMAP_USABLE)
    {
      continue;
    }

    if (entry == NULL) 
    {
      entry = current_entry;
    } 
    else if (current_entry->base > entry->base) 
    {
      entry = current_entry;
    }
  }

  return entry;
}

static size_t get_bitmap_size(void)
{
  struct limine_memmap_entry *highest_entry = find_highest_mmap_entry();
  
  if (highest_entry == NULL)    /* Sanity check */
  {
    panic("pmm: Could not find highest entry!\n");
  }

  size_t highest_address = highest_entry->base + highest_entry->length;
  size_t bitmap_size = ALIGN_UP((highest_address/0x1000)/8, 0x1000);
  return bitmap_size;
}

static void init_bitmap(void)
{
  size_t bitmap_size = get_bitmap_size();
  kinfo("Required bitmap size: %d bytes\n", bitmap_size);

  /* Find a memory segment for our bitmap */
  for (size_t i = 0; i < mmap_resp->entry_count; ++i)
  {
    struct limine_memmap_entry *entry = mmap_resp->entries[i];

    if (entry->type != LIMINE_MEMMAP_USABLE)
    {
      continue;
    }

    if (entry->length >= bitmap_size)
    {
      bitmap = (uint8_t *)(entry->base + VMM_HIGHER_HALF);
      memset(bitmap, 0xFF, bitmap_size);
      entry->length -= bitmap_size;
      entry->base += bitmap_size;    
      break;
    }
  }

  kinfo("Found space for bitmap at %x\n", bitmap);

  /* Setup bitmap based on memory map */
  for (size_t i = 0; i < mmap_resp->entry_count; ++i) 
  {
    struct limine_memmap_entry* entry = mmap_resp->entries[i];
    if (entry->type == LIMINE_MEMMAP_USABLE && entry->length >= bitmap_size) 
    {
      for (size_t j = 0; j < entry->length; j += 0x1000) 
      {
        bitmap_unset_bit(bitmap, (entry->base + j)/0x1000);
      }
    }
  }
}

static size_t find_contiguous_chunk(size_t frame_count)
{
  size_t frames_found = 0;
  size_t bit_start = 0;
  for (size_t bit = 1; bit < get_bitmap_size()*8; ++bit)
  {
    if (!bitmap_test_bit(bitmap, bit))
    {
      bit_start = bit;
      ++frames_found;
    }
    else
    {
      bit_start = 0;
      frames_found = 0;
    }

    if (frames_found >= frame_count)
    {
      return bit_start;
    }
  }

  return bit_start;
}

uintptr_t pmm_alloc(size_t frames)
{
  size_t bit = find_contiguous_chunk(frames);
  if (bit == 0)
  {
    return 0;
  }

  for (size_t i = bit; i < bit+frames; ++i)
  {
    bitmap_set_bit(bitmap, i);
  }

  return PAGE_SIZE*bit;
}

void pmm_free(uintptr_t ptr, size_t frames)
{
  for (size_t i = ptr; i < ptr+(frames*PAGE_SIZE); i += PAGE_SIZE)
  {
    bitmap_unset_bit(bitmap, i/0x1000);
  }
}

void pmm_init(void)
{
  mmap_resp = mmap_req.response;
  init_bitmap();
}
