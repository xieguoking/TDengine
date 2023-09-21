/*
 * Copyright (c) 2019 TAOS Data, Inc. <xsren@taosdata.com>
 *
 * This program is free software: you can use, redistribute, and/or modify
 * it under the terms of the GNU Affero General Public License, version 3
 * or later ("AGPL"), as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.
 *
 * You should have received a copy of the GNU Affero General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 */

// TAOS standard API example. The same syntax as MySQL, but only a subset
// to compile: gcc -o demo demo.c -ltaos

#include <stdio.h>
#include <stdlib.h>
#include <taos.h>
#include <windows.h>

int  stop_sign = 0;
int  total_run_test_count = 1;

void run_test_sql(int cnt ) {
  TAOS* taos = NULL;
  printf("[%d]run_test_sql thread id is %d..........\n", cnt, GetCurrentThreadId());
  if (taos == NULL) taos = taos_connect("127.0.0.1", "root", "taosdata", NULL, 6030);
  if (taos != NULL) {
    char sql_file[301];
    GetCurrentDirectoryA(280, sql_file);
    strcat(sql_file, "\\write_sql.txt");
    printf("[%d][TID:%d]Loading SQL from file: %s\n", cnt, GetCurrentThreadId(), sql_file);
    char* sql = (char*)malloc(1024 * 1024);
    sql[0] = 0;
    // 从当前工作目录中的文件 write_sql.txt 加载需要执行的SQL语句。
    FILE* fp = fopen(sql_file, "rb");
    if (fp) {
      int  sk = fseek(fp, 0, SEEK_SET);
      auto st = fread(sql, 1, 1024 * 1024, fp);
      fclose(fp);
      if (st >= 0) {
        sql[st] = 0;
      }
    }
    int j = 0;
    while (j++ < 1)  // 这里无论是循环调用taos_query一次或多次，都会出现资源句柄无法释放的问题。
    {
      TAOS_RES* run_result = taos_query(taos, "use test;");
      TAOS_RES* run_result = taos_query(taos, "select * from tb;");
      if (taos_errno(run_result) != 0) {
        fprintf(stderr, "\n[%d]-->Error when running.Reason:\n  %d : %s\n", GetCurrentThreadId(),
                taos_errno(run_result), taos_errstr(run_result));
        taos_free_result(run_result);
        break;
      }
      taos_free_result(run_result);
      Sleep(10);
    }
    taos_close(taos);
    free(sql);
  }
  printf("[%d][TID:%d]run_test_sql finished...\n", cnt, GetCurrentThreadId());  // 注意观察任务管理器中本进程的句柄数量
}
void manage_run_test_sql() {
   while (true) {
    printf("Begin the batch index [%d] thread..........\n", total_run_test_count);
    run_test_sql(total_run_test_count++);
    Sleep(500);
  }
  puts("manage_run_test_sql() finished....");
}
BOOL WINAPI MyHandlerRoutine(DWORD dwCtrlType) {
  /*
  //for windows
  switch (signal_type) {
  case  CTRL_C_EVENT://  0
  case  CTRL_BREAK_EVENT://   1
  case  CTRL_CLOSE_EVENT://  2
  case  CTRL_LOGOFF_EVENT:// 5
  case  CTRL_SHUTDOWN_EVENT:// 6
  }*/
  if (dwCtrlType == CTRL_C_EVENT) {
    puts("Got CTRL_C_EVENT!");
    stop_sign = 1;
  }
  return TRUE;
}

int main(int argc, char* argv[]) {
  taos_init();

  bool run_query_in_main_thread = false;
  if (argc > 1 && strcmp(argv[1], "-m") == 0) {
    run_query_in_main_thread = true;
  }

  if (run_query_in_main_thread) {  // No resource leakage issues when taos_query is called in the main thread
    printf("\n====Press Enter to call 'taos_query()' once in Main Thread====\n\nMain Thread ID is :%d\n\n",
    GetCurrentThreadId());
    while (true) {
      run_test_sql(1);
      Sleep(500);
    }
  } else {
    SetConsoleCtrlHandler(MyHandlerRoutine, TRUE);
    puts("\n====Press Enter to start a new Child Thread====\n");
    _beginthread(manage_run_test_sql, 0, NULL);
    while (stop_sign == 0) Sleep(1000);
  }
  return 0;
}