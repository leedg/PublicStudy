# assets_C — C-Style draw.io PNG Exports

draw.io desktop app으로 `Network_Async_DB_Report_img_C/` 폴더의 `.drawio` 파일을
PNG로 내보낸 후 이 폴더에 저장합니다.

## 필요한 파일 목록
- diag_arch.png
- diag_seq.png
- diag_async_1_dispatch.png
- diag_async_2_keyed.png
- diag_async_3_execqueue.png
- diag_db.png

## C-Style 문서 빌드
```
cd Doc/Reports/_scripts
python Build-NetworkAsyncDB-Report.py ../Network_Async_DB_Report_C.docx C
```
