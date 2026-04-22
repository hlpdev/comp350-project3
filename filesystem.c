#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/types.h>

////// constants

#define TOTAL_BLOCKS 100    // Total # of blocks
#define FREEMAP_BLOCKS 10   // # of blocks used for the freemap
#define FILE_BLOCKS 90      // # of blocks used for files

#define BLOCK_SIZE 512      // the size of each block
#define FILENAME_SIZE 32    // the size/length of a block's filename string
#define DATA_SIZE (BLOCK_SIZE - FILENAME_SIZE)  // the length of the data buffer for each block

////// structs

// represents a single block, stores filename and a data buffer
typedef struct {
  char name[FILENAME_SIZE];
  uint8_t data[DATA_SIZE];
} block;

// represents our disk
//
// blocks & (freemap + files) occupy the same memory
// added union so we can freely access freemap and files
// while still using the same memory from blocks
typedef struct {
  union {
    block blocks[TOTAL_BLOCKS];
    struct {
      uint8_t freemap[FREEMAP_BLOCKS * BLOCK_SIZE];
      block files[FILE_BLOCKS];
    };
  };
} disk;

// represents our filesystem
// our file table + disk
typedef struct {
  char file_table[TOTAL_BLOCKS][FILENAME_SIZE];
  disk disk;
} filesystem;


////// freemap & block utilities 

// gets the freemap status of the
// specified block
static bool freemap_get(filesystem* fs, uint8_t block) {
  return (fs->disk.freemap[block / 8] >> (block % 8)) & 1; // reads our freemap and returns the free bit for the specified block index
}

// sets the specified block index to 
// either used or unused in the freemap
static void freemap_set(filesystem* fs, uint8_t block, bool used) {
  uint8_t* byte = &fs->disk.freemap[block / 8];

  if (used) {
    *byte |= (1 << (block % 8));
  } else {
    *byte &= ~(1 << (block % 8));
  }
}

// gets the index of a free block
// or -1 if there are no free blocks
static int16_t find_free_block(filesystem* fs) {
  for (uint8_t i = FREEMAP_BLOCKS; i < TOTAL_BLOCKS; i++) {
    if (!freemap_get(fs, i)) {
      return i; // block isn't being used, return the index
    }
  }

  return -1; // no free block found, return -1
}

// returns the index of the block with the
// specified filename or -1 if not found
static int16_t find_file(filesystem* fs, const char* name) {
  // loop through the freemap until we find an unused block
  for (uint8_t i = FREEMAP_BLOCKS; i < TOTAL_BLOCKS; i++) {
    if (freemap_get(fs, i) && strcmp(fs->file_table[i], name) == 0) {
      return i; // block is being used and the filename equals the file we're looking for, return the block index
    }
  }

  return -1; // file not found, return -1
}

////// commands
// prefixed with fs_ because some standard library
// functions use the same names as our commands

// zeros out the entire filesystem struct, essentially wiping / formatting the disk
static void fs_format(filesystem* fs) {
  memset(fs, 0, sizeof(filesystem)); // sets the entire filesystem struct's memory region to 0
  
  printf("disk formatted\n");
}

static void fs_create(filesystem* fs, char* name) {
  if (find_file(fs, name) != -1) { // if we find a file with that name, it already exists
    printf("'%s' already exists\n", name);
    return;
  }

  int16_t block_idx = find_free_block(fs); // find the next free block index
  if (block_idx == -1) {
    printf("disk full!\n"); // if we can't find a free block, then the disk is full
    return;
  }

  freemap_set(fs, block_idx, true); // set the block as used in the freemap

  strncpy(fs->file_table[block_idx], name, FILENAME_SIZE - 1); // copy filename to file table
  strncpy(fs->disk.files[block_idx - FREEMAP_BLOCKS].name, name, FILENAME_SIZE - 1); // copy filename to block filename

  printf("created '%s' at block %d\n", name, block_idx);
}

static void fs_read(filesystem* fs, char* name) {
  int16_t block_idx = find_file(fs, name); // find the block index of the specified file
  if (block_idx == -1) { // if the result is -1, then we couldn't find the file to read
    printf("'%s' not found\n", name);
    return;
  }

  // print out the block's data buffer as a string
  printf("%s\n", (char*)fs->disk.files[block_idx - FREEMAP_BLOCKS].data);
}

static void fs_write(filesystem* fs, char* name, char* data) {
  int16_t block_idx = find_file(fs, name); // find the block index of the specified file
  if (block_idx == -1) { // if the result is -1, then we couldn't find the file to write to
    printf("'%s' not found\n", name);
    return;
  }

  block* blk = &fs->disk.files[block_idx - FREEMAP_BLOCKS]; // get pointer to actual block

  memset(blk->data, 0, DATA_SIZE); // zero out any existing data / wipe existing data
  strncpy((char*)blk->data, data, DATA_SIZE - 1); // copy over input data to block data

  printf("wrote to '%s'\n", name);
}

static void fs_delete(filesystem* fs, char* name) {
  int16_t block_idx = find_file(fs, name); // find the block index of the specified file
  if (block_idx == -1) { // if the result is -1, then we couldn't find the file to delete
    printf("'%s' not found\n", name);
    return;
  }

  freemap_set(fs, block_idx, false); // set the block to unused in the freemap
  memset(&fs->disk.files[block_idx - FREEMAP_BLOCKS], 0, sizeof(block)); // zero out / wipe the data on the block
  memset(fs->file_table[block_idx], 0, FILENAME_SIZE); // zero out / wipe the filename from the file table

  printf("deleted '%s'\n", name);
}

// loop through all of the file blocks, and if a block is used, print it's filename
static void fs_ls(filesystem* fs) {
  uint8_t count = 0;
  for (uint8_t i = FREEMAP_BLOCKS; i < TOTAL_BLOCKS; i++) {
    if (freemap_get(fs, i)) {
      printf("%s\n", fs->file_table[i]);
      count++;
    }
  }

  if (!count) { // if we didn't find any files, print filesystem empty
    printf("filesystem empty\n");
  }
}

////// entrypoint

int main(void) {
  filesystem fs;
  fs_format(&fs); // format the disk on startup

  // large size to account for file contents, name, and command
  char input[DATA_SIZE + FILENAME_SIZE + 16];
  
  printf("Welcome to the COMP350 filesystem simulator!\n");
  printf("Type 'help' to view available commands\n");

  while (true) {
    printf("> ");

    if (!fgets(input, sizeof(input), stdin)) {
      break;
    }

    input[strcspn(input, "\n")] = '\0'; // remove \n from the input

    char* cmd = strtok(input, " "); // get the first argument (our command)
    char* arg1 = strtok(NULL, " "); // get the second argument (our filename)
    char* arg2 = strtok(NULL, ""); // get the rest of the line (for write contents)
    
    if (!cmd) continue; // if the command is empty, just continue

    if (strcmp(cmd, "format") == 0) {
      fs_format(&fs);
      continue;
    }

    if (strcmp(cmd, "create") == 0) {
      if (!arg1) { // if they didn't specify a filename, print usage
        printf("usage: create <filename>\n");
        continue;
      }

      fs_create(&fs, arg1);
      continue;
    }

    if (strcmp(cmd, "read") == 0) {
      if (!arg1) { // if they didn't specify a filename, print usage
        printf("usage: read <filename>\n");
        continue;
      }

      fs_read(&fs, arg1);
      continue;
    }

    if (strcmp(cmd, "write") == 0) {
      if (!arg1 || !arg2) { // if they didn't specify a filename or file contents, print usage
        printf("usage: write <filename> <contents>\n");
        continue;
      }

      fs_write(&fs, arg1, arg2);
      continue;
    }

    if (strcmp(cmd, "delete") == 0) {
      if (!arg1) { // if they didn't specify a filename, print usage
        printf("usage: delete <filename>\n");
        continue;
      }

      fs_delete(&fs, arg1);
      continue;
    }

    if (strcmp(cmd, "ls") == 0) {
      fs_ls(&fs);
      continue;
    }

    if (strcmp(cmd, "exit") == 0) {
      break;
    }

    // added clear command for fun, clears the console using fork, execvp, and waitpid
    if (strcmp(cmd, "clear") == 0) {
      pid_t pid = fork();

      if (pid == 0) {
        char* argv[] = { "clear", NULL };
        execvp("clear", argv);
        exit(1);
      } else if (pid > 0) {
        waitpid(pid, NULL, 0);
      }

      continue;
    }

    // print our command list
    if (strcmp(cmd, "help") == 0) {
      printf("COMP350 Filesystem Simulator\n");
      printf("Available commands:\n");

      // %-24s is the same as %s in printf, but just adds formatting so everything is lined up
      printf("  %-24s%s\n", "format", "zeros out the entire disk and file table");
      printf("  %-24s%s\n", "create <name>", "allocates the first free block and associates it with <name>");
      printf("  %-24s%s\n", "read <name>", "prints the data of the file with <name>");
      printf("  %-24s%s\n", "write <name> <contents>", "overwrites the file <name> with <contents>");
      printf("  %-24s%s\n", "delete <name>", "frees the block and removes name from the file table\n");
      printf("  %-24s%s\n", "ls", "lists all files currently on the disk");
      printf("  %-24s%s\n", "clear", "clears the terminal");
      printf("  %-24s%s\n", "exit", "exits the program");
      printf("  %-24s%s\n", "help", "shows this help screen");
      continue;
    }

    // if we get here, the command they typed doesn't exist
    printf("unknown command: '%s'\n", cmd);
  }

  return 0;
}
