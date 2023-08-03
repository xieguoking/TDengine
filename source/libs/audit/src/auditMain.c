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

static SAudit tsAudit = {0};
static char* tsAuditUri = "/audit";

void auditSend(SJson *pJson) {
  char *pCont = tjsonToString(pJson);
  uDebug("report cont:%s\n", pCont);
  if (pCont != NULL) {
    EHttpCompFlag flag = tsAudit.cfg.comp ? HTTP_GZIP : HTTP_FLAT;
    if (taosSendHttpReport(tsAudit.cfg.server, tsAuditUri, tsAudit.cfg.port, pCont, strlen(pCont), flag) != 0) {
      uError("failed to send monitor msg");
    }
    taosMemoryFree(pCont);
  }
}
