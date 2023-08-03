/*
 * Copyright (c) 2019 TAOS Data, Inc. <jhtao@taosdata.com>
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

#define _DEFAULT_SOURCE
#include "auditInt.h"
#include "taoserror.h"
#include "thttp.h"
#include "ttime.h"
#include "tjson.h"
#include "tglobal.h"

static SAudit tsAudit = {0};
static char* tsAuditUri = "/audit";

int32_t auditInit(const SAuditCfg *pCfg) {
  tsAudit.cfg = *pCfg;
  return 0;
}

void auditRecord(char *user, char *oper, char *detail) {
  if (!tsEnableAudit || tsAuditFqdn[0] == 0 || tsAuditPort == 0) return;
  SJson *pJson = tjsonCreateObject();
  if (pJson == NULL) {
    terrno = TSDB_CODE_OUT_OF_MEMORY;
    return;
  }

  char   buf[40] = {0};
  int64_t curTime = taosGetTimestampMs();
  taosFormatUtcTime(buf, sizeof(buf), curTime, TSDB_TIME_PRECISION_MILLI);

  tjsonAddStringToObject(pJson, "ts", buf);
  tjsonAddStringToObject(pJson, "user", user);
  tjsonAddStringToObject(pJson, "operation", oper);
  tjsonAddStringToObject(pJson, "detail", detail);

  auditSend(pJson);
}

void auditSend(SJson *pJson) {
  char *pCont = tjsonToString(pJson);
  uDebug("audit record cont:%s\n", pCont);
  if (pCont != NULL) {
    EHttpCompFlag flag = tsAudit.cfg.comp ? HTTP_GZIP : HTTP_FLAT;
    if (taosSendHttpReport(tsAudit.cfg.server, tsAuditUri, tsAudit.cfg.port, pCont, strlen(pCont), flag) != 0) {
      uError("failed to send audit msg");
    }
    taosMemoryFree(pCont);
  }
}
