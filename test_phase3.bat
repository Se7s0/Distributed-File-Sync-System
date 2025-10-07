@echo off
REM Phase 3 Event System Test Script for Windows
REM Make sure the server is running first!

echo ================================================================
echo Phase 3 Event System Test
echo ================================================================
echo.

set SERVER_URL=http://localhost:8080

echo Test 1: Get Homepage
echo --------------------------------------------------
curl -s %SERVER_URL%/
echo.
echo.

echo Test 2: Add first file (triggers FileAddedEvent)
echo --------------------------------------------------
curl -s -X POST %SERVER_URL%/metadata/add -d "FILE \"/test.txt\" HASH \"abc123\" SIZE 1024 MODIFIED 1704096000 STATE SYNCED"
echo.
echo.

echo Test 3: Add second file
echo --------------------------------------------------
curl -s -X POST %SERVER_URL%/metadata/add -d "FILE \"/document.pdf\" HASH \"def456\" SIZE 2048 MODIFIED 1704096000 STATE SYNCED"
echo.
echo.

echo Test 4: Add third file
echo --------------------------------------------------
curl -s -X POST %SERVER_URL%/metadata/add -d "FILE \"/photo.jpg\" HASH \"xyz789\" SIZE 4096 MODIFIED 1704096000 STATE SYNCED"
echo.
echo.

echo Test 5: List all files
echo --------------------------------------------------
curl -s %SERVER_URL%/metadata/list
echo.
echo.

echo Test 6: Update a file (triggers FileModifiedEvent)
echo --------------------------------------------------
curl -s -X PUT %SERVER_URL%/metadata/update -d "FILE \"/test.txt\" HASH \"new_hash_updated\" SIZE 2048 MODIFIED 1704096100 STATE SYNCED"
echo.
echo.

echo Test 7: Delete a file (triggers FileDeletedEvent)
echo --------------------------------------------------
curl -s -X DELETE %SERVER_URL%/metadata/delete/test.txt
echo.
echo.

echo Test 8: List remaining files
echo --------------------------------------------------
curl -s %SERVER_URL%/metadata/list
echo.
echo.

echo ================================================================
echo All tests completed!
echo ================================================================
echo.
echo Check the server console to see the event logs:
echo   [FileAdded] events
echo   [FileModified] events
echo   [FileDeleted] events
echo.
echo Press Ctrl+C in the server window to see final metrics!
pause
