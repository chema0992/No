#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <ctype.h>
#include <limits.h>
#include <stdint.h>

#define CONTEXT_SIZE 16
#define COMMAND_SIZE 32

typedef struct {
    FILE *file;
    unsigned char buffer[CONTEXT_SIZE];
    size_t buffer_len;
    long context_pos;
    size_t pos;
    const char *filename;
    int modified;
} Editor;

static void cmdw(Editor *editor);
static void err_exit(Editor *editor);
static void cmdp(Editor *editor);
static void cmdnr(Editor *editor);
static void cmdnl(Editor *editor);
static void cmdi(Editor *editor, unsigned char value);
static void cmdd(Editor *editor);
static void cmdq(Editor *editor);
static void cmdm(Editor *editor);
static void handle_command(Editor *editor, char *command);
static int hex_value(char ch);

int main(int argc, char *argv[])
{
    if (argc != 2) {
        return EXIT_FAILURE;
    }

    Editor editor = {0};
    editor.filename = argv[1];
    editor.file = fopen(editor.filename, "rb");

    if (editor.file == NULL) {
        return EXIT_FAILURE;
    }

    editor.buffer_len = fread(editor.buffer, 1, CONTEXT_SIZE, editor.file);
    if (ferror(editor.file)) {
        fclose(editor.file);
        return EXIT_FAILURE;
    }

    editor.context_pos = 0;
    editor.pos = 0;
    editor.modified = 0;

    char command[COMMAND_SIZE];

    while (1) {
        if (fgets(command, sizeof(command), stdin) == NULL) {
            err_exit(&editor);
        }

        size_t command_len = strlen(command);

        if (command_len > 0 && command[command_len - 1] == '\n') {
            command[--command_len] = '\0';
        } else if (!feof(stdin)) {
            int ch;
            while ((ch = getchar()) != '\n' && ch != EOF) {
                /* 명령어 길면 과감하게 삭제. */
            }
            err_exit(&editor);
        }

        if (command_len > 0 && command[command_len - 1] == '\r') {
            command[--command_len] = '\0';
        }

        if (command[0] == '\0') {
            continue;
        }

        handle_command(&editor, command);
    }
}

static void handle_command(Editor *editor, char *command)
{
    size_t len = strlen(command);

    /* 숫자 뒤에 b가 붙어야함 */
    if (len >= 2 && command[len - 1] == 'b') {
        int all_digits = 1;
        for (size_t i = 0; i < len - 1; i++) {
            if (command[i] < '0' || command[i] > '9') {
                all_digits = 0;
                break;
            }
        }

        if (all_digits) {
            command[len - 1] = '\0';
            char *end = NULL;
            long value = strtol(command, &end, 10);

            if (command[0] == '\0' || end == command || *end != '\0' ||
                value < 1 || (unsigned long)value > editor->buffer_len) {
                err_exit(editor);
            }

            editor->pos = (size_t)value;
            return;
        }
    }

    if (strcmp(command, "p") == 0) {
        cmdp(editor);
    } else if (strcmp(command, "nr") == 0) {
        cmdnr(editor);
    } else if (strcmp(command, "nl") == 0) {
        cmdnl(editor);
    } else if (strcmp(command, "m") == 0) {
        cmdm(editor);
    } else if (strcmp(command, "w") == 0) {
        cmdw(editor);
    } else if (strcmp(command, "q") == 0) {
        cmdq(editor);
    } else if (strcmp(command, "d") == 0) {
        cmdd(editor);
    } else if (command[0] == 'i') {
        unsigned char value;

        if (len == 2) {
            /* iX로 X 삽입. i는 공백 */
            value = (unsigned char)command[1];
        } else if (len == 3 && command[1] == '\\' && command[2] == 'n') {
            /* i\n으로 줄바꿈 삽입 */
            value = (unsigned char)'\n';
        } else if (len == 5 && command[1] == '\\' && command[2] == 'x' &&
                   isxdigit((unsigned char)command[3]) &&
                   isxdigit((unsigned char)command[4])) {
            int high = hex_value(command[3]);
            int low = hex_value(command[4]);
            value = (unsigned char)((high << 4) | low);
        } else {
            err_exit(editor);
        }

        cmdi(editor, value);
    } else {
        /* 모르면 과감하게 컷 */
        err_exit(editor);
    }
}

static void cmdp(Editor *editor)
{
    if (editor->pos < 1 || editor->pos > editor->buffer_len) {
        err_exit(editor);
    }

    if (putchar(editor->buffer[editor->pos - 1]) == EOF) {
        err_exit(editor);
    }
}

static void cmdnr(Editor *editor)
{
    /* 저장후 종료하세요 */
    if (editor->modified || editor->buffer_len < CONTEXT_SIZE) {
        err_exit(editor);
    }

    if (editor->context_pos > LONG_MAX - CONTEXT_SIZE) {
        err_exit(editor);
    }

    long next_pos = editor->context_pos + CONTEXT_SIZE;

    if (fseek(editor->file, next_pos, SEEK_SET) != 0) {
        err_exit(editor);
    }

    size_t next_len = fread(editor->buffer, 1, CONTEXT_SIZE, editor->file);
    if (next_len == 0 || ferror(editor->file)) {
        err_exit(editor);
    }

    editor->context_pos = next_pos;
    editor->buffer_len = next_len;
    editor->pos = 0;
    editor->modified = 0;
}

static void cmdnl(Editor *editor)
{
    if (editor->modified || editor->context_pos == 0) {
        err_exit(editor);
    }

    long previous_pos = editor->context_pos - CONTEXT_SIZE;

    if (fseek(editor->file, previous_pos, SEEK_SET) != 0) {
        err_exit(editor);
    }

    size_t previous_len = fread(editor->buffer, 1, CONTEXT_SIZE, editor->file);
    if (previous_len == 0 || ferror(editor->file)) {
        err_exit(editor);
    }

    editor->context_pos = previous_pos;
    editor->buffer_len = previous_len;
    editor->pos = 0;
    editor->modified = 0;
}

static void cmdm(Editor *editor)
{
    long file_end;

    if (editor->modified ||
        editor->buffer_len != CONTEXT_SIZE ||
        editor->context_pos < 0 ||
        editor->context_pos > LONG_MAX - CONTEXT_SIZE) {
        err_exit(editor);
    }

    if (fseek(editor->file, 0, SEEK_END) != 0) {
        err_exit(editor);
    }

    file_end = ftell(editor->file);

    if (file_end < 0 ||
        file_end != editor->context_pos + CONTEXT_SIZE) {
        err_exit(editor);
    }

    editor->context_pos += CONTEXT_SIZE;
    editor->buffer_len = 0;
    editor->pos = 0;
    editor->modified = 0;
}

static void cmdi(Editor *editor, unsigned char value)
{
    if (editor->buffer_len >= CONTEXT_SIZE || editor->pos > editor->buffer_len) {
        err_exit(editor);
    }

    /* 선택된 바이트가 없으면 맨 앞에 삽입, 아니면 뒤에 삽입 */
    size_t insert_at = (editor->pos == 0) ? 0 : editor->pos;

    memmove(&editor->buffer[insert_at + 1],
            &editor->buffer[insert_at],
            editor->buffer_len - insert_at);

    editor->buffer[insert_at] = value;
    editor->buffer_len++;
    editor->pos = insert_at + 1;
    editor->modified = 1;
}

static void cmdd(Editor *editor)
{
    if (editor->pos < 1 || editor->pos > editor->buffer_len) {
        err_exit(editor);
    }

    memmove(&editor->buffer[editor->pos - 1],
            &editor->buffer[editor->pos],
            editor->buffer_len - editor->pos);

    editor->buffer_len--;
    editor->pos = 0;
    editor->modified = 1;
}

static void cmdq(Editor *editor)
{
    fclose(editor->file);
    exit(EXIT_SUCCESS);
}

static void err_exit(Editor *editor)
{
    cmdw(editor);
}

static void cmdw(Editor *editor)
{
    size_t filename_len = strlen(editor->filename);
    if (filename_len > SIZE_MAX - sizeof(".no.tmp")) {
        fclose(editor->file);
        exit(EXIT_FAILURE);
    }

    size_t temp_size = filename_len + sizeof(".no.tmp");
    char *temp_name = malloc(temp_size);
    if (temp_name == NULL) {
        fclose(editor->file);
        exit(EXIT_FAILURE);
    }

    int name_result = snprintf(temp_name, temp_size, "%s.no.tmp", editor->filename);
    if (name_result < 0 || (size_t)name_result >= temp_size) {
        free(temp_name);
        fclose(editor->file);
        exit(EXIT_FAILURE);
    }

    struct stat original_stat;
    if (stat(editor->filename, &original_stat) != 0) {
        free(temp_name);
        fclose(editor->file);
        exit(EXIT_FAILURE);
    }

    FILE *temp = fopen(temp_name, "wb");
    if (temp == NULL) {
        free(temp_name);
        fclose(editor->file);
        exit(EXIT_FAILURE);
    }

    int failed = 0;

    if (editor->context_pos < 0 ||
        editor->context_pos % CONTEXT_SIZE != 0 ||
        editor->buffer_len > CONTEXT_SIZE ||
        fseek(editor->file, 0, SEEK_SET) != 0) {
        failed = 1;
    }

    /* 현재 컨텍스트 이전의 모든 바이트를 복사 */
    for (long i = 0; !failed && i < editor->context_pos; i++) {
        int byte = fgetc(editor->file);
        if (byte == EOF || fputc(byte, temp) == EOF) {
            failed = 1;
        }
    }

    if (!failed &&
        fwrite(editor->buffer, 1, editor->buffer_len, temp) != editor->buffer_len) {
        failed = 1;
    }

    /* 현재 컨텍스트의 이전 버전 스킵 */
    for (size_t i = 0; !failed && i < CONTEXT_SIZE; i++) {
        int byte = fgetc(editor->file);
        if (byte == EOF) {
            if (ferror(editor->file)) {
                failed = 1;
            }
            break;
        }
    }

    /* 나머지 복사!! */
    while (!failed) {
        size_t count = fread(editor->buffer, 1, CONTEXT_SIZE, editor->file);

        if (count > 0 && fwrite(editor->buffer, 1, count, temp) != count) {
            failed = 1;
            break;
        }

        if (count < CONTEXT_SIZE) {
            if (ferror(editor->file)) {
                failed = 1;
            }
            break;
        }
    }

    if (fclose(temp) != 0) {
        failed = 1;
    }
    if (fclose(editor->file) != 0) {
        failed = 1;
    }

    /* 원본 파일의 권한 비트를 유지 */
    if (!failed && chmod(temp_name, original_stat.st_mode & 07777) != 0) {
        failed = 1;
    }

    if (failed) {
        remove(temp_name);
        free(temp_name);
        exit(EXIT_FAILURE);
    }

    /* rename으로 원본 파일명 대체 */
    if (rename(temp_name, editor->filename) != 0) {
        remove(temp_name);
        free(temp_name);
        exit(EXIT_FAILURE);
    }

    free(temp_name);
    exit(EXIT_SUCCESS);
}

static int hex_value(char ch)
{
    if (ch >= '0' && ch <= '9') {
        return ch - '0';
    }
    if (ch >= 'a' && ch <= 'f') {
        return ch - 'a' + 10;
    }
    if (ch >= 'A' && ch <= 'F') {
        return ch - 'A' + 10;
    }
    return 0;
}
