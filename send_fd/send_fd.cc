#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
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
  int fds[2];
  CHECK(socketpair(AF_UNIX, SOCK_STREAM, 0, fds) == 0);
  CHECK(fcntl(fds[0], F_SETFL, O_NONBLOCK) == 0);
  CHECK(fcntl(fds[1], F_SETFL, O_NONBLOCK) == 0);

  for (int i = 0; i < 1000000; i++) {
    const size_t kNumFDs = 1;

    char buf[CMSG_SPACE(kNumFDs * sizeof(int))];
    struct msghdr msg = {};
    msg.msg_control = buf;
    msg.msg_controllen = sizeof(buf);
    struct cmsghdr* cmsg = CMSG_FIRSTHDR(&msg);
    cmsg->cmsg_level = SOL_SOCKET;
    cmsg->cmsg_type = SCM_RIGHTS;
    cmsg->cmsg_len = CMSG_LEN(sizeof(int) * kNumFDs);
    int new_fd = dup(fds[0]);
    CHECK(new_fd != -1);
    CHECK(new_fd != fds[0]);
    reinterpret_cast<int*>(CMSG_DATA(cmsg))[0] = new_fd;
    msg.msg_controllen = cmsg->cmsg_len;
    close(new_fd);

    ssize_t result = sendmsg(fds[0], &msg, MSG_NOSIGNAL);
    CHECK(result <= 0);
    if (result != 0) {
      perror(argv[0]);
      return 1;
    }
  }

  return 0;
}
