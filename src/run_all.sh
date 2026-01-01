# #!/usr/bin/env bash
# set -euo pipefail

# # 確保在 run_all.sh 所在的 src/ 目錄執行
# BASEDIR="$(cd "$(dirname "$0")" && pwd)"
# cd "$BASEDIR"

# echo "Working dir: $BASEDIR"

# # 1) 編譯（只編一次）
# echo "Compiling bmatch..."
# g++ -std=c++11 -O2 -Wall -Wextra \
#   main.cpp parser.cpp simulator.cpp match.cpp match_writer.cpp \
#   -o bmatch

# # 2) log
# LOGDIR="./run_logs"
# rm -rf "$LOGDIR"
# mkdir -p "$LOGDIR"

# run_case () {
#   local casedir="$1"
#   local casename
#   casename="$(basename "$casedir")"

#   echo "=== Running $casename === ($casedir)"

#   ./bmatch "$casedir/input" "output" \
#     >"$LOGDIR/$casename.stdout" \
#     2>"$LOGDIR/$casename.stderr" || true

#   if [[ -f "$casedir/output" ]]; then
#     echo "  -> output generated: $casedir/output"
#   else
#     echo "  -> output MISSING:  $casedir/output"
#   fi
# }

# # 3) 自動找出所有 case01~case20 的資料夾（不管在 release/ hidden/ cases_20230614/ 任何位置）
# #    條件：資料夾名是 caseNN，且裡面有 input 檔
# mapfile -t CASE_DIRS < <(
#   find "$BASEDIR" .. -type f -name input 2>/dev/null \
#   | sed 's|/input$||' \
#   | awk -F/ '
#       {
#         n=NF;
#         if ($n ~ /^case[0-9][0-9]$/) print $0
#       }' \
#   | sort -u
# )

# echo "Found cases with input: ${#CASE_DIRS[@]}"

# if [[ ${#CASE_DIRS[@]} -eq 0 ]]; then
#   echo "[ERROR] No case directories found. (No */caseNN/input under $BASEDIR or ..)"
#   exit 1
# fi

# # 4) 跑全部找到的 case（通常會是 case01~case20）
# for casedir in "${CASE_DIRS[@]}"; do
#   run_case "$casedir"
# done

# echo "All cases done."
#!/bin/bash
set -e

# Compile and run boolean matching.
#
# Assumptions:
#   - You are in the repository root.
#   - Source files are under src/ (adjust if your layout differs).
#   - MiniSAT is available under third_party/minisat (as used by sat_wrap.cpp).

mkdir -p build

g++ -std=c++11 -O2 -Wall -Wextra \
    -Isrc -Ithird_party/minisat \
    src/main.cpp src/parser.cpp src/simulator.cpp src/match.cpp src/match_writer.cpp \
    src/unate_table.cpp \
    src/sat_wrap.cpp src/sat_check.cpp \
    -o build/bmatch

# Examples:
#   ./build/bmatch testcase/case01/input testcase/case01/output.match
#   ./build/bmatch testcase/case01/input testcase/case01/output.match testcase/case01/unate_c01_1.csv testcase/case01/unate_c01_2.csv

if [ $# -ge 2 ]; then
  if [ $# -ge 4 ]; then
    ./build/bmatch "$1" "$2" "$3" "$4"
  else
    ./build/bmatch "$1" "$2"
  fi
fi
