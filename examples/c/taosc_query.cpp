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
#include <string.h>
#include <taos.h>
#include <windows.h>
#include <thread>

int  main_run_test_sql_stop_sign = 0;
int  total_run_test_count = 1;
void run_test_sql(int cnt) {
  printf("run onece..");
  TAOS* taos = NULL;
  printf("[%d]run_test_sql thread id is %d..........\n", cnt, GetCurrentThreadId());
  if (taos == NULL) taos = taos_connect("127.0.0.1", "root", "taosdata", NULL, 6030);
  if (taos != NULL) {
    // char sql_file[301];
    // GetCurrentDirectoryA(280, sql_file);
    // strcat(sql_file, "\\write_sql.txt");
    // printf("[%d][TID:%d]Loading SQL from file: %s\n", cnt, GetCurrentThreadId(), sql_file);
    // char* sql = (char*)malloc(1024 * 1024);
    // sql[0] = 0;
    // // 从当前工作目录中的文件 write_sql.txt 加载需要执行的SQL语句。
    // FILE* fp = fopen(sql_file, "rb");
    // if (fp) {
    //   int  sk = fseek(fp, 0, SEEK_SET);
    //   auto st = fread(sql, 1, 1024 * 1024, fp);
    //   fclose(fp);
    //   if (st >= 0) {
    //     sql[st] = 0;
    //   }
    // }
    // int j = 0;
    // while (j++ < 0)  // 这里无论是循环调用taos_query一次或多次，都会出现资源句柄无法释放的问题。
    // {
    //   TAOS_RES* run_result = taos_query(taos, sql);
    //   if (taos_errno(run_result) != 0) {
    //     fprintf(stderr, "\n[%d]-->Error when running.Reason:\n%s\n", GetCurrentThreadId(), taos_errstr(run_result));
    //     taos_free_result(run_result);
    //     break;
    //   }
    //   taos_free_result(run_result);
    //   Sleep(10);
    // }
    // printf("[%d][TID:%d]run_test_sql free taos start...\n", cnt, GetCurrentThreadId());
    // free(sql);
    taos_close(taos);
    printf("[%d][TID:%d]run_test_sql free taos end.\n", cnt, GetCurrentThreadId());
  } else {
    printf("[%d][TID:%d]run_test_sql connect failed.\n", cnt, GetCurrentThreadId());
  }
  printf("[%d][TID:%d]run_test_sql finished...\n", cnt, GetCurrentThreadId());  // 注意观察任务管理器中本进程的句柄数量
}
void manage_run_test_sql() {
  while (getchar()) {
    Sleep(500);
    if (main_run_test_sql_stop_sign) break;
    printf("Begin the batch index [%d] thread..........\n", total_run_test_count);
    std::thread(&run_test_sql, total_run_test_count++).join();
    // run_test_sql(total_run_test_count++);
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
    main_run_test_sql_stop_sign = 1;
  }
  return TRUE;
}
int main(int argc, char* argv[]) {
  taos_init();
  /****************************************************
  是否在主线程中运行taos_query ? 在编译之前改变这个bool值，生成不同的EXE，用以复现资源泄漏问题
  ******************************************************/
  bool run_taos_query_in_main_thread = false;
  if (argc > 1 && strcmp(argv[1], "-m") == 0) {
    run_taos_query_in_main_thread = true;
  }

  if (run_taos_query_in_main_thread) {  // 当taos_query在主线程中被调用时，无资源泄漏问题
    printf("\n====Press Enter to call 'taos_query()' once in Main Thread====\n\nMain Thread ID is :%d\n\n",
    GetCurrentThreadId());
    run_test_sql(1);
    getchar();
    run_test_sql(2);
    getchar();
    run_test_sql(3);
    getchar();
    run_test_sql(4);
    getchar();
    run_test_sql(5);
    getchar();
    run_test_sql(6);
    getchar();
    run_test_sql(7);
    getchar();
    run_test_sql(8);
    getchar();
    run_test_sql(9);
    getchar();
  } else {  // 当taos_query在子线程中被调用时，有资源泄漏问题。WINDOWS任务管理器中本进程的句柄数量会一直在增加
    SetConsoleCtrlHandler(MyHandlerRoutine, TRUE);
    puts("\n====Press Enter to start a new Child Thread====\n");
    auto tt1 = std::thread(&manage_run_test_sql);
    while (main_run_test_sql_stop_sign == 0) {
      Sleep(5000);
    }
    tt1.join();
  }
  return 0;
}
