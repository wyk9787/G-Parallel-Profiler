#define _GNU_SOURCE
#include <math.h>
#include <openssl/md5.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define NUM_THREAD 4
#define MAX_USERNAME_LENGTH 24
#define PASSWORD_LENGTH 6
#define TOTAL_PASSWORDS (pow(26.0, PASSWORD_LENGTH))

// Use this struct to pass arguments to our threads
typedef struct thread_args {
  int arg;
} thread_args_t;

// use this struct to receive results from our threads
typedef struct thread_result {
  int result;
} thread_result_t;

typedef struct password_entry {
  char username[MAX_USERNAME_LENGTH + 1];
  uint8_t password_md5[MD5_DIGEST_LENGTH + 1];
  bool cracked;
  struct password_entry *next;
} password_entry_t;

void crack_passwords(char *plaintext);
void generate_all_possibilities(size_t number);
int md5_string_to_bytes(const char *md5_string, uint8_t *bytes);
password_entry_t *read_password_file(const char *filename);
void print_md5_bytes(const uint8_t *bytes);

password_entry_t *passwords;

// pthread_mutex_t m = PTHREAD_MUTEX_INITIALIZER;

/**
 * This is our thread function. When we call pthread_create with this as an
 * argument
 * a new thread is created to run this thread in parallel with the program's
 * main
 * thread. When passing parameters to thread functions or accepting return
 * values we
 * have to jump through a few hoops because POSIX threads can only take and
 * return
 * a void*.
 */
void *thread_fn(void *void_args) {
  // Case the args pointer to the appropriate type and print our argument
  thread_args_t *args = (thread_args_t *)void_args;
  generate_all_possibilities(args->arg);
  // Return the pointer to allocated memory to our parent thread.
  return NULL;
}

int main(int argc, char **argv) {
  if (argc != 2) {
    fprintf(stderr, "Usage: %s <path to password directory file>\n", argv[0]);
    exit(1);
  }

  // Read in the password file
  passwords = read_password_file(argv[1]);

  // Initilization
  password_entry_t *current = passwords;
  while (current != NULL) {
    current->cracked = false;
    current = current->next;
  }
  // You'll have to move over any code from partB that you would like to use.
  // Here's a quick little thread demo.
  pthread_t threads[NUM_THREAD];

  // Make two structs so we can pass arguments to our threads
  thread_args_t thread_args[NUM_THREAD];

  // Create threads
  for (size_t i = 0; i < NUM_THREAD; i++) {
    thread_args[i].arg = i * TOTAL_PASSWORDS / 4;
    if (pthread_create(&threads[i], NULL, thread_fn, &thread_args[i]) != 0) {
      perror("Error creating thread 1");
      exit(2);
    }
  }

  // Make pointers to the thread result structs that our threads will write into
  // thread_result_t *thread_result[NUM_THREAD];

  // Wait threads to join
  for (size_t i = 0; i < NUM_THREAD; i++) {
    if (pthread_join(threads[i], NULL) != 0) {
      perror("Error joining with thread 1");
      exit(2);
    }
  }

  // // Show the results
  // printf("Thread 1 returned %d\n", thread1_result->result);
  // printf("Thread 2 retunred %d\n", thread2_result->result);
  //
  // // Free the result structs, which were originally allocated in our threads
  // free(thread1_result);
  // free(thread2_result);
}

void generate_all_possibilities(size_t number) {
  int cur_digit = PASSWORD_LENGTH - 1;
  size_t len = TOTAL_PASSWORDS / 4;
  char guess[7] = "aaaaaa";
  size_t end = number + len;
  for (size_t i = number; i < end; i++) {
    size_t num = i;
    while (num > 0) {
      guess[cur_digit--] = num % 26 + 'a';
      num /= 26;
    }
    cur_digit = PASSWORD_LENGTH - 1;

    crack_passwords(guess);
  }
}

void crack_passwords(char *plaintext) {
  uint8_t password_hash[MD5_DIGEST_LENGTH];
  MD5((unsigned char *)plaintext, strlen(plaintext), password_hash);

  // Check if the two hashes are equal
  password_entry_t *current = passwords;
  bool all_true = true;
  while (current != NULL) {
    if (!current->cracked) {  // Has not been cracked yet
      if (memcmp(current->password_md5, password_hash, MD5_DIGEST_LENGTH) ==
          0) {
        printf("%s ", current->username);
        printf("%s\n", plaintext);
        // pthread_mutex_lock(&m);
        current->cracked = true;
        // pthread_mutex_unlock(&m);
      } else {
        all_true = false;
      }
    }
    current = current->next;
  }
  if (all_true) {
    exit(0);
  }
}

/**
 * Read a file of username and MD5 passwords. Return a linked list
 * of entries.
 * \param filename  The path to the password file
 * \returns         A pointer to the first node in the password list
 */
password_entry_t *read_password_file(const char *filename) {
  // Open the password file
  FILE *password_file = fopen(filename, "r");
  if (password_file == NULL) {
    perror("opening password file");
    exit(2);
  }

  // Keep track of the current list
  password_entry_t *list = NULL;

  // Read until we hit the end of the file
  while (!feof(password_file)) {
    // Make space for a new node
    password_entry_t *newnode =
        (password_entry_t *)malloc(sizeof(password_entry_t));

    // Make space to hold the MD5 string
    char md5_string[MD5_DIGEST_LENGTH * 2 + 1];

    // Try to read. The space in the format string is required to eat the
    // newline
    if (fscanf(password_file, "%s %s ", newnode->username, md5_string) != 2) {
      fprintf(stderr, "Error reading password file: malformed line\n");
      exit(2);
    }

    // Convert the MD5 string to MD5 bytes in our new node
    if (md5_string_to_bytes(md5_string, newnode->password_md5) != 0) {
      fprintf(stderr, "Error reading MD5\n");
      exit(2);
    }

    // Add the new node to the front of the list
    newnode->next = list;
    list = newnode;
  }

  return list;
}

/**
 * Convert a string representation of an MD5 hash to a sequence
 * of bytes. The input md5_string must be 32 characters long, and
 * the output buffer bytes must have room for MD5_DIGEST_LENGTH
 * bytes.
 *
 * \param md5_string  The md5 string representation
 * \param bytes       The destination buffer for the converted md5 hash
 * \returns           0 on success, -1 otherwise
 */
int md5_string_to_bytes(const char *md5_string, uint8_t *bytes) {
  // Check for a valid MD5 string
  if (strlen(md5_string) != 2 * MD5_DIGEST_LENGTH) return -1;

  // Start our "cursor" at the start of the string
  const char *pos = md5_string;

  // Loop until we've read enough bytes
  for (size_t i = 0; i < MD5_DIGEST_LENGTH; i++) {
    // Read one byte (two characters)
    int rc = sscanf(pos, "%2hhx", &bytes[i]);
    if (rc != 1) return -1;

    // Move the "cursor" to the next hexadecimal byte
    pos += 2;
  }

  return 0;
}

/**
 * Print a byte array that holds an MD5 hash to standard output.
 *
 * \param bytes   An array of bytes from an MD5 hash function
 */
void print_md5_bytes(const uint8_t *bytes) {
  for (size_t i = 0; i < MD5_DIGEST_LENGTH; i++) {
    printf("%02hhx", bytes[i]);
  }
}
