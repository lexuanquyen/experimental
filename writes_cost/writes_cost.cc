#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/uio.h>
#include <unistd.h>

#include <vector>

#define CHECK(x) \
    do { \
      if (!(x)) { \
        fputs("CHECK failed: " #x "\n", stderr); \
        abort(); \
      } \
    } while (false)

int main(int argc, char** argv) {
  if (argc != 4) {
    fprintf(stderr, "usage: %s MODE HEADER_SIZE DATA_SIZE\n\n"
        "MODE:\n"
        "  one_write -- one write()\n"
        "  two_writes -- two writes()\n"
        "  two_copies_one_write -- two memcpy()s and one write()\n"
        "  one_writev -- one writev() (two buffers)\n"
        "  one_writev_split_data -- one writev() (three buffers; data split)\n"
        "  one_writev_one_buffer -- one writev() (one buffer)\n"
        "  one_send -- one send()\n"
        "  one_sendmsg -- one sendmsg() (two buffers)\n",
        argv[0]);
    return 1;
  }

  enum {
    kDoOneWrite, kDoTwoWrites, kDoTwoCopiesOneWrite, kDoOneWritev,
    kDoOneWritevSplitData, kDoOneWritevOneBuffer, kDoOneSend, kDoOneSendmsg
  } do_what;
  if (strcmp(argv[1], "one_write") == 0)
    do_what = kDoOneWrite;
  else if (strcmp(argv[1], "two_writes") == 0)
    do_what = kDoTwoWrites;
  else if (strcmp(argv[1], "two_copies_one_write") == 0)
    do_what = kDoTwoCopiesOneWrite;
  else if (strcmp(argv[1], "one_writev") == 0)
    do_what = kDoOneWritev;
  else if (strcmp(argv[1], "one_writev_split_data") == 0)
    do_what = kDoOneWritevSplitData;
  else if (strcmp(argv[1], "one_writev_one_buffer") == 0)
    do_what = kDoOneWritevSplitData;
  else if (strcmp(argv[1], "one_send") == 0)
    do_what = kDoOneSend;
  else if (strcmp(argv[1], "one_sendmsg") == 0)
    do_what = kDoOneSendmsg;
  else
    CHECK(false);

  size_t header_size = size_t(atoi(argv[2]));
  CHECK(header_size > 0 && header_size <= 100 * 1024);
  size_t data_size = size_t(atoi(argv[3]));
  CHECK(data_size > 0 && data_size <= 100 * 1024 * 1024);
  size_t total_size = header_size + data_size;

  printf("mode=%s, header_size=%zu, data_size=%zu\n",
         argv[1], header_size, data_size);

  int fds[2];
  CHECK(socketpair(AF_UNIX, SOCK_STREAM, 0, fds) == 0);
  CHECK(fcntl(fds[0], F_SETFL, O_NONBLOCK) == 0);
  CHECK(fcntl(fds[1], F_SETFL, O_NONBLOCK) == 0);

  std::vector<char> header(header_size, 'h');
  std::vector<char> data(data_size, 'd');
  std::vector<char> total(header_size + data_size);
  memcpy(&total[0], &header[0], header_size);
  memcpy(&total[header_size], &data[0], data_size);
  std::vector<char> read_buffer(total_size);

#ifdef __APPLE__
  static const int kSendFlags = 0;
#else
  static const int kSendFlags = MSG_NOSIGNAL;
#endif
  for (int i = 0; i < 1000000; i++) {
    switch (do_what) {
      case kDoOneWrite:
        CHECK(write(fds[0], &total[0], total_size) == ssize_t(total_size));
        break;
      case kDoTwoWrites:
        CHECK(write(fds[0], &header[0], header_size) == ssize_t(header_size));
        CHECK(write(fds[0], &data[0], data_size) == ssize_t(data_size));
        break;
      case kDoTwoCopiesOneWrite:
        memcpy(&total[0], &header[0], header_size);
        memcpy(&total[header_size], &data[0], data_size);
        CHECK(write(fds[0], &total[0], total_size) == ssize_t(total_size));
        break;
      case kDoOneWritev: {
        struct iovec iov[2] = {
          { &header[0], header_size },
          { &data[0], data_size }
        };
        CHECK(writev(fds[0], iov, 2) == ssize_t(total_size));
        break;
      }
      case kDoOneWritevSplitData: {
        struct iovec iov[3] = {
          { &header[0], header_size },
          { &data[0], data_size / 2 },
          { &data[data_size / 2], data_size - (data_size / 2) }
        };
        CHECK(writev(fds[0], iov, 3) == ssize_t(total_size));
        break;
      }
      case kDoOneWritevOneBuffer: {
        struct iovec iov[1] = {
          { &total[0], total_size }
        };
        CHECK(writev(fds[0], iov, 1) == ssize_t(total_size));
        break;
      }
      case kDoOneSend:
        CHECK(send(fds[0], &total[0], total_size, kSendFlags) ==
                   ssize_t(total_size));
        break;
      case kDoOneSendmsg: {
        struct iovec iov[2] = {
          { &header[0], header_size },
          { &data[0], data_size }
        };
        struct msghdr msg = { NULL, 0, iov, 2, NULL, 0, 0 };
        CHECK(sendmsg(fds[0], &msg, kSendFlags) == ssize_t(total_size));
        break;
      }
    }
    CHECK(read(fds[1], &read_buffer[0], total_size) == ssize_t(total_size));
  }

  CHECK(close(fds[0]) == 0);
  CHECK(close(fds[1]) == 0);

  return 0;
}
