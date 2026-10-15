#define _GNU_SOURCE

#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <unistd.h>

#define CONTEXT_SIZE 4
#define COMMAND_CAPACITY 6

typedef struct {
    const char *filename;
    off_t context_pos;
    int fd;
    unsigned char buffer[CONTEXT_SIZE];
    unsigned char buffer_len;
    unsigned char pos;
    unsigned char modified;
} Editor;

static void cmdw(Editor *editor);
static void err_exit(Editor *editor);
static void cmdn(Editor *editor);
static void cmdm(Editor *editor);
static void cmdd(Editor *editor);
static void cmdi(Editor *editor, unsigned char value);
static void handle_command(Editor *editor, char *command);
static int load_context(Editor *editor, off_t position);
static int read_command(char *command, size_t capacity);
static int read_one(int fd, unsigned char *value);
static int write_all(int fd, const unsigned char *data, size_t length);
static int hex_value(char value);
static void save_failure(Editor *editor, int temp_fd, char *temp_name);

int main(int argc, char **argv)
{
    Editor editor = {0};
    char command[COMMAND_CAPACITY];

    if (argc != 2) {
        return EXIT_FAILURE;
    }

    editor.filename = argv[1];
    editor.fd = open(editor.filename, O_RDONLY);
    if (editor.fd < 0) {
        return EXIT_FAILURE;
    }

    if (load_context(&editor, 0) != 0) {
        (void)close(editor.fd);
        return EXIT_FAILURE;
    }

    for (;;) {
        int result = read_command(command, sizeof(command));

        if (result <= 0) {
            err_exit(&editor);
        }
        if (command[0] != '\0') {
            handle_command(&editor, command);
        }
    }
}

/* 컨텍스트 하나만 읽기 */
static int load_context(Editor *editor, off_t position)
{
    if (position < 0 || lseek(editor->fd, position, SEEK_SET) == (off_t)-1) {
        return -1;
    }

    editor->buffer_len = 0;
    while (editor->buffer_len < CONTEXT_SIZE) {
        ssize_t count = read(editor->fd,
                             editor->buffer + editor->buffer_len,
                             CONTEXT_SIZE - editor->buffer_len);
        if (count > 0) {
            editor->buffer_len = (unsigned char)(editor->buffer_len + count);
        } else if (count == 0) {
            break;
        } else if (errno != EINTR) {
            return -1;
        }
    }

    editor->context_pos = position;
    editor->pos = 0;
    editor->modified = 0;
    return 0;
}

static int read_command(char *command, size_t capacity)
{
    size_t length = 0;
    int too_long = 0;

    for (;;) {
        unsigned char value;
        ssize_t count = read(STDIN_FILENO, &value, 1);

        if (count < 0) {
            if (errno == EINTR) {
                continue;
            }
            return -1;
        }
        if (count == 0) {
            if (length == 0 && !too_long) {
                return 0;
            }
            break;
        }
        if (value == '\n') {
            break;
        }
        if (too_long) {
            continue;
        }
        if (value == '\0' || length + 1 >= capacity) {
            too_long = 1;
            continue;
        }
        command[length++] = (char)value;
    }

    if (too_long) {
        return -1;
    }
    if (length > 0 && command[length - 1] == '\r') {
        --length;
    }
    command[length] = '\0';
    return 1;
}

static void handle_command(Editor *editor, char *command)
{
    size_t length = 0;
    while (command[length] != '\0') {
        ++length;
    }

    /* 바이트 선택 */
    if (length >= 2 && command[length - 1] == 'b') {
        unsigned int value = 0;

        for (size_t i = 0; i + 1 < length; ++i) {
            unsigned char digit = (unsigned char)command[i];
            if (digit < '0' || digit > '9') {
                err_exit(editor);
            }
            value = value * 10U + (unsigned int)(digit - '0');
            if (value > CONTEXT_SIZE) {
                err_exit(editor);
            }
        }

        if (value == 0 || value > editor->buffer_len) {
            err_exit(editor);
        }
        editor->pos = (unsigned char)value;
        return;
    }

    if (length == 1 && command[0] == 'n') {
        cmdn(editor);
    } else if (length == 1 && command[0] == 'd') {
        cmdd(editor);
    } else if (length == 1 && command[0] == 'm') {
        cmdm(editor);
    } else if (length == 1 && command[0] == 'w') {
        cmdw(editor);
    } else if (length >= 2 && command[0] == 'i') {
        unsigned char value;

        if (length == 2) {
            value = (unsigned char)command[1];
        } else if (length == 3 && command[1] == '\\' && command[2] == 'n') {
            value = (unsigned char)'\n';
        } else if (length == 5 && command[1] == '\\' && command[2] == 'x') {
            int high = hex_value(command[3]);
            int low = hex_value(command[4]);
            if (high < 0 || low < 0) {
                err_exit(editor);
            }
            value = (unsigned char)((high << 4) | low);
        } else {
            err_exit(editor);
        }
        cmdi(editor, value);
    } else {
        err_exit(editor);
    }
}

/* n은 다음 컨텍스트 */
static void cmdn(Editor *editor)
{
    struct stat info;

    if (editor->modified || editor->buffer_len != CONTEXT_SIZE ||
        editor->context_pos < 0 || fstat(editor->fd, &info) != 0 ||
        info.st_size < editor->context_pos ||
        info.st_size - editor->context_pos <= CONTEXT_SIZE) {
        err_exit(editor);
    }

    if (load_context(editor, editor->context_pos + CONTEXT_SIZE) != 0 ||
        editor->buffer_len == 0) {
        err_exit(editor);
    }
}

/* m은 새 컨텍스트 */
static void cmdm(Editor *editor)
{
    struct stat info;

    if (editor->modified || editor->buffer_len != CONTEXT_SIZE ||
        editor->context_pos < 0 || editor->context_pos % CONTEXT_SIZE != 0 ||
        fstat(editor->fd, &info) != 0 || info.st_size < editor->context_pos ||
        info.st_size - editor->context_pos != CONTEXT_SIZE) {
        err_exit(editor);
    }

    editor->context_pos = info.st_size;
    editor->buffer_len = 0;
    editor->pos = 0;
    editor->modified = 0;
}

static void cmdd(Editor *editor)
{
    size_t index;

    if (editor->pos == 0 || editor->pos > editor->buffer_len) {
        err_exit(editor);
    }

    index = (size_t)editor->pos - 1;
    while (index + 1 < editor->buffer_len) {
        editor->buffer[index] = editor->buffer[index + 1];
        ++index;
    }

    --editor->buffer_len;
    editor->pos = 0;
    editor->modified = 1;
}

/* 삽입 */
static void cmdi(Editor *editor, unsigned char value)
{
    size_t insert_at;
    size_t i;

    if (editor->buffer_len >= CONTEXT_SIZE || editor->pos > editor->buffer_len) {
        err_exit(editor);
    }

    insert_at = editor->pos == 0 ? 0 : editor->pos;
    i = editor->buffer_len;
    while (i > insert_at) {
        editor->buffer[i] = editor->buffer[i - 1];
        --i;
    }

    editor->buffer[insert_at] = value;
    ++editor->buffer_len;
    editor->pos = (unsigned char)(insert_at + 1);
    editor->modified = 1;
}

static int hex_value(char value)
{
    if (value >= '0' && value <= '9') {
        return value - '0';
    }
    if (value >= 'a' && value <= 'f') {
        return value - 'a' + 10;
    }
    if (value >= 'A' && value <= 'F') {
        return value - 'A' + 10;
    }
    return -1;
}

static int read_one(int fd, unsigned char *value)
{
    for (;;) {
        ssize_t count = read(fd, value, 1);
        if (count == 1) {
            return 1;
        }
        if (count == 0) {
            return 0;
        }
        if (errno != EINTR) {
            return -1;
        }
    }
}

static int write_all(int fd, const unsigned char *data, size_t length)
{
    size_t written = 0;

    while (written < length) {
        ssize_t count = write(fd, data + written, length - written);
        if (count > 0) {
            written += (size_t)count;
        } else if (count < 0 && errno == EINTR) {
            continue;
        } else {
            return -1;
        }
    }
    return 0;
}

static void err_exit(Editor *editor)
{
    cmdw(editor);
}

static void cmdw(Editor *editor)
{
    static const char suffix[] = ".nos.tmp.XXXXXX";
    struct stat original_stat;
    size_t filename_length = 0;
    char *temp_name;
    int temp_fd;

    if (editor->fd < 0 || editor->context_pos < 0 ||
        editor->context_pos % CONTEXT_SIZE != 0 ||
        editor->buffer_len > CONTEXT_SIZE ||
        fstat(editor->fd, &original_stat) != 0 ||
        editor->context_pos > original_stat.st_size) {
        save_failure(editor, -1, NULL);
    }

    while (editor->filename[filename_length] != '\0') {
        ++filename_length;
    }
    if (filename_length > SIZE_MAX - sizeof(suffix)) {
        save_failure(editor, -1, NULL);
    }

    temp_name = (char *)malloc(filename_length + sizeof(suffix));
    if (temp_name == NULL) {
        save_failure(editor, -1, NULL);
    }
    for (size_t i = 0; i < filename_length; ++i) {
        temp_name[i] = editor->filename[i];
    }
    for (size_t i = 0; i < sizeof(suffix); ++i) {
        temp_name[filename_length + i] = suffix[i];
    }

    temp_fd = mkstemp(temp_name);
    if (temp_fd < 0) {
        free(temp_name);
        save_failure(editor, -1, NULL);
    }

    if (lseek(editor->fd, 0, SEEK_SET) == (off_t)-1) {
        save_failure(editor, temp_fd, temp_name);
    }

    for (off_t i = 0; i < editor->context_pos; ++i) {
        unsigned char value;
        if (read_one(editor->fd, &value) != 1 ||
            write_all(temp_fd, &value, 1) != 0) {
            save_failure(editor, temp_fd, temp_name);
        }
    }

    if (write_all(temp_fd, editor->buffer, editor->buffer_len) != 0) {
        save_failure(editor, temp_fd, temp_name);
    }

    /* Skip the ostatic void cmdw(Editor *editor)
{
    static const char suffix[] = ".nos.tmp.XXXXXX";

    struct stat original_stat;
    size_t filename_length = 0;
    char *temp_name;
    int temp_fd;

    */

    /* 편집 컨텍스트와 별개의 임시 저장 버퍼 */
    unsigned char io_buffer[4096];

    if (editor->fd < 0 || editor->context_pos < 0 ||
        editor->context_pos % CONTEXT_SIZE != 0 ||
        editor->buffer_len > CONTEXT_SIZE ||
        fstat(editor->fd, &original_stat) != 0 ||
        editor->context_pos > original_stat.st_size) {
        save_failure(editor, -1, NULL);
    }

    while (editor->filename[filename_length] != '\0') {
        ++filename_length;
    }

    if (filename_length > SIZE_MAX - sizeof(suffix)) {
        save_failure(editor, -1, NULL);
    }

    temp_name = (char *)malloc(filename_length + sizeof(suffix));
    if (temp_name == NULL) {
        save_failure(editor, -1, NULL);
    }

    for (size_t i = 0; i < filename_length; ++i) {
        temp_name[i] = editor->filename[i];
    }

    for (size_t i = 0; i < sizeof(suffix); ++i) {
        temp_name[filename_length + i] = suffix[i];
    }

    temp_fd = mkstemp(temp_name);
    if (temp_fd < 0) {
        free(temp_name);
        save_failure(editor, -1, NULL);
    }

    if (lseek(editor->fd, 0, SEEK_SET) == (off_t)-1) {
        save_failure(editor, temp_fd, temp_name);
    }

    /*
     * 1. 현재 컨텍스트 앞부분 복사
     *    최대 4096바이트씩 읽고 씀.
     */
    off_t remaining = editor->context_pos;

    while (remaining > 0) {
        size_t wanted =
            remaining > (off_t)sizeof(io_buffer)
                ? sizeof(io_buffer)
                : (size_t)remaining;

        size_t received = 0;

        /* 요청한 크기를 전부 읽을 때까지 반복 */
        while (received < wanted) {
            ssize_t amount;

            do {
                amount = read(
                    editor->fd,
                    io_buffer + received,
                    wanted - received
                );
            } while (amount < 0 && errno == EINTR);

            if (amount <= 0) {
                save_failure(editor, temp_fd, temp_name);
            }

            received += (size_t)amount;
        }

        if (write_all(temp_fd, io_buffer, wanted) != 0) {
            save_failure(editor, temp_fd, temp_name);
        }

        remaining -= (off_t)wanted;
    }

    /*
     * 2. 수정된 현재 컨텍스트 기록
     *    컨텍스트 크기는 기존대로 최대 4바이트.
     */
    if (write_all(
            temp_fd,
            editor->buffer,
            editor->buffer_len
        ) != 0) {
        save_failure(editor, temp_fd, temp_name);
    }

    /*
     * 3. 원본의 기존 컨텍스트 건너뛰기
     *    읽어서 버리지 않고 lseek()으로 이동.
     */
    off_t old_context_bytes =
        original_stat.st_size - editor->context_pos;

    if (old_context_bytes > (off_t)CONTEXT_SIZE) {
        old_context_bytes = (off_t)CONTEXT_SIZE;
    }

    off_t tail_pos = editor->context_pos + old_context_bytes;

    if (lseek(editor->fd, tail_pos, SEEK_SET) == (off_t)-1) {
        save_failure(editor, temp_fd, temp_name);
    }

    /*
     * 4. 현재 컨텍스트 뒤쪽 복사
     *    한 번에 최대 4096바이트.
     */
    for (;;) {
        ssize_t amount;

        do {
            amount = read(
                editor->fd,
                io_buffer,
                sizeof(io_buffer)
            );
        } while (amount < 0 && errno == EINTR);

        if (amount < 0) {
            save_failure(editor, temp_fd, temp_name);
        }

        if (amount == 0) {
            break;
        }

        if (write_all(
                temp_fd,
                io_buffer,
                (size_t)amount
            ) != 0) {
            save_failure(editor, temp_fd, temp_name);
        }
    }

    /*
     * 5. 권한 보존 및 파일 교체
     */
    if (fchmod(temp_fd, original_stat.st_mode & 07777) != 0) {
        save_failure(editor, temp_fd, temp_name);
    }

    if (close(temp_fd) != 0) {
        temp_fd = -1;
        save_failure(editor, temp_fd, temp_name);
    }

    if (close(editor->fd) != 0) {
        editor->fd = -1;
        save_failure(editor, -1, temp_name);
    }

    editor->fd = -1;

    if (syscall(
            SYS_renameat2,
            AT_FDCWD, temp_name,
            AT_FDCWD, editor->filename,
            0
        ) != 0) {
        save_failure(editor, -1, temp_name);
    }

    free(temp_name);
    exit(EXIT_SUCCESS);
}

static void save_failure(Editor *editor, int temp_fd, char *temp_name)
{
    if (temp_fd >= 0) {
        (void)close(temp_fd);
    }
    if (editor->fd >= 0) {
        (void)close(editor->fd);
        editor->fd = -1;
    }
    if (temp_name != NULL) {
        (void)unlink(temp_name);
        free(temp_name);
    }
    exit(EXIT_FAILURE);
}
