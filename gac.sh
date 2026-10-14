#!/bin/bash

# 1. 변경된 모든 파일 스테이징
git add .

# 2. 사용자에게 커밋 메시지 입력 받기 (-p 옵션은 안내 문구를 띄워줍니다)
read -p "커밋 메시지를 입력하세요: " commit_message

# 3. 입력 값이 비어있는지 확인
if [ -z "$commit_message" ]; then
    echo "오류: 커밋 메시지가 비어 있어 작업을 취소합니다."
    exit 1
fi

# 4. 커밋 실행
git commit -m "$commit_message"

# 5. 원격 저장소로 푸시 (브랜치명은 환경에 맞게 main 또는 master 등으로 수정하세요)
git push origin main

echo "작업이 성공적으로 완료되었습니다!"

