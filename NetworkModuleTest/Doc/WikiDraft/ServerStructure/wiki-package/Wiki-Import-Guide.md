# Wiki Import Guide (Draft -> GitHub Wiki)

## 0) 패키지 생성
```powershell
.\Doc\WikiDraft\ServerStructure\scripts\build-wiki-package.ps1 -IncludePng
```

생성 결과:
- `Doc/WikiDraft/ServerStructure/wiki-package`

## 1) Wiki 저장소 클론
```powershell
git clone https://github.com/<owner>/<repo>.wiki.git
```

## 2) 페이지 파일 복사
`Doc/WikiDraft/ServerStructure/wiki-package` 안의 파일을 Wiki 루트로 그대로 복사:
- `Home.md`
- `_Sidebar.md`
- `01-Overall-Architecture.md`
- `02-Session-Layer.md`
- `03-Packet-and-AsyncDB-Flow.md`
- `04-Graceful-Shutdown.md`
- `05-Reconnect-Strategy.md`
- `Wiki-Import-Guide.md`
- `assets/*`

## 3) 링크 확인
- 문서 내 `./assets/*.svg` 경로가 유지되는지 확인
- Wiki에서 Mermaid 렌더링이 필요한 페이지는 코드 블록이 정상 표시되는지 확인

## 4) 커밋/푸시
```powershell
git add .
git commit -m "docs: add server structure wiki draft with diagrams"
git push
```

## 5) 첫 공개 후 점검
1. 모바일 화면에서 다이어그램 가독성 확인
2. 포트/실행 순서/백오프 설명 최신 상태 확인
3. 코드 변경 시 `검증일` 갱신
