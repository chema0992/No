#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CONTEXT_SIZE 16
#define COMMAND_SIZE 32

int main(int argc, char *argv[])
{
    if (argc != 2) {
        return 1;
    }

    // 파일 열기
    FILE *file = fopen(argv[1], "r+b");

    if (file == NULL) {
        return 1;
    }

    // 현재 컨텍스트
    unsigned char buffer[CONTEXT_SIZE];

    // 현재 컨텍스트에 실제로 들어있는 바이트 수
    size_t buffer_len = fread(buffer, 1, CONTEXT_SIZE, file);

    // 현재 위치
    size_t pos = 0;

    // 명령 입력 버퍼
    char command[COMMAND_SIZE];

    while (1) {
        if (fgets(command, sizeof(command), stdin) == NULL) {
            break;
        }

        // '\n' 제거
        command[strcspn(command, "\n")] = '\0';

        // 빈 입력 무시
        if (command[0] == '\0') {
            continue;
        }

        // 마지막 문자가 'b'인지 확인
        size_t command_len = strlen(command);

        if (command[command_len - 1] == 'b') {
            // b 제거
            command[command_len - 1] = '\0';

            // 앞부분을 숫자로 변환
            pos = atoi(command);

            // 1 ~ 16만 허용
            if (pos < 1 || pos > CONTEXT_SIZE) {
                fclose(file);
                return 0;
            }

            // 현재 pos 출력
            printf("pos = %zu\n", pos);
        }
    }

    fclose(file);

    return 0;
}
