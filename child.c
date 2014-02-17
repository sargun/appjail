#include "common.h"
#include "cap.h"
#include "child.h"
#include "opts.h"
#include "home.h"
#include <sys/mount.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <limits.h>
#include <string.h>

static void set_mount_propagation() {
  /* Make our mount a slave of the host - this will make sure all
   * new mounts propagate from the host, but our mounts do not
   * propagate to the host
   */
  if( cap_mount(NULL, "/", NULL, MS_REC | MS_SLAVE, NULL) == -1)
    errExit("mount --make-rslave /");
}

static void setup_proc() {
  /* Mount our own local /proc - we have our own PID namespace,
   * so this doesn't give away information regarding the host.
   *
   * The host's /proc/mounts is still visible, sadly.
   */
  if( cap_umount2("/proc", MNT_DETACH) == -1 && errno != EINVAL)
    errExit("umount /proc");
  if( cap_mount("proc", "/proc", "proc", 0, NULL) == -1)
    errExit("mount -t proc proc /proc");
}

static void setup_path(const char *name, const char *path, mode_t mode) {
  char p[PATH_MAX];

  snprintf(p, PATH_MAX-1, "./%s", name);
  if( mkdir(p, mode) == -1 )
    errExit("mkdir");
  if( chmod(p, mode) == -1 )
    errExit("chmod");
  if( cap_umount2(path, MNT_DETACH) == -1 && errno != EINVAL)
    errExit("umount2");
  if( cap_mount(p, path, NULL, MS_BIND, NULL) == -1 )
    errExit("mount --bind");
  if( cap_mount(NULL, path, NULL, MS_PRIVATE, NULL) == -1)
    errExit("mount --make-rprivate");
}

static void get_tty() {
  const char *console;
  int fd;

  /* Get name of the current TTY */
  if( (console = ttyname(0)) == NULL )
    errExit("ttyname()");
  /* create a dummy file to mount to */
  if((fd = open("console", O_CREAT|O_RDWR, 0)) == -1)
    errExit("open()");
  close(fd);
  /* Make the current TTY accessible in APPJAIL_SWAPDIR/console */
  if( cap_mount(console, "console", NULL, MS_BIND, NULL) == -1)
    errExit("mount --bind $TTY APPJAIL_SWAPDIR/console");
  /* Make the console bind private */
  if( cap_mount(NULL, "console", NULL, MS_PRIVATE, NULL) == -1)
    errExit("mount --make-private APPJAIL_SWAPDIR/console");
}

static void setup_tty() {
  int fd;

  if( cap_mount("console", "/dev/console", NULL, MS_MOVE, NULL) == -1)
    errExit("mount --bind APPJAIL_SWAPDIR/console /dev/console");

  /* The current TTY is now accessible under /dev/console,
   * however, the original device (like /dev/pts/0) will not
   * be accessible in the container. Reopen /dev/console as our
   * standard input, output and error.
   */
  if((fd = open("/dev/console", O_RDWR)) == -1)
    errExit("open(/dev/console)");
  close(0);
  close(1);
  close(2);
  dup2(fd, 0);
  dup2(fd, 1);
  dup2(fd, 2);
  close(fd);
}

static void setup_devpts() {
  if( cap_umount2("/dev/pts", MNT_DETACH) == -1 && errno != EINVAL)
    errExit("umount2");
  if( cap_mount("devpts", "/dev/pts", "devpts", 0, "newinstance,gid=5,mode=620,ptmxmode=0666") == -1)
    errExit("mount devpts");
  if( cap_mount("/dev/pts/ptmx", "/dev/ptmx", NULL, MS_BIND, NULL) == -1 )
    errExit("mount --bind");
}

static void setup_shm() {
  if( cap_umount2("/dev/shm", MNT_DETACH) == -1 && errno != EINVAL)
    errExit("umount2");
  if( cap_mount("shm", "/dev/shm", "tmpfs", MS_NODEV | MS_NOSUID, "mode=1777,uid=0,gid=0") == -1)
    errExit("mount shm");
}

int child_main(void *arg) {
  char tmpdir[PATH_MAX];
  appjail_options *opts = (appjail_options*)arg;

  drop_caps();
  set_mount_propagation();
  /* Create temporary directory */
  strncpy(tmpdir, "/tmp/appjail-XXXXXX", PATH_MAX-1);
  if( mkdtemp(tmpdir) == NULL )
    errExit("mkdtemp");
  /* Bind the temporary directory to APPJAIL_SWAPDIR
   * This isn't nice, but we need a directory that we won't touch */
  if( cap_mount(tmpdir, APPJAIL_SWAPDIR, NULL, MS_BIND, NULL) == -1 )
    errExit("mount --bind TMPDIR APPJAIL_SWAPDIR");
  /* Change into the temporary directory */
  if(chdir(APPJAIL_SWAPDIR) == -1)
    errExit("chdir()");

  /* Bind directories and files that may disappear */
  get_home_directory(opts->homedir);
  get_tty();

  /* set up our private mounts */
  setup_proc();
  setup_path("tmp", "/tmp", 01777);
  setup_path("vartmp", "/var/tmp", 01777);
  setup_path("home", "/home", 0755);
  setup_devpts();
  setup_shm();

  /* set up the tty */
  setup_tty();
  /* set up home directory using the one we bound earlier */
  setup_home_directory();

  /* unmount our temporary directory */
  if( cap_umount2(APPJAIL_SWAPDIR, 0) == -1 )
    errExit("umount APPJAIL_SWAPDIR");

  /* Make some permissions consistent */
  cap_chown("/tmp", 0, 0);
  cap_chown("/var/tmp", 0, 0);
  cap_chown("/home", 0, 0);

  /* We drop all capabilities from the permitted capability set */
  drop_caps_forever();

  if(opts->argv[0] != NULL) {
    execvp(opts->argv[0], opts->argv);
    errExit("execvp");
  }
  else {
    execl("/bin/sh", "/bin/sh", "-i", NULL);
    errExit("execl");
  }
}
