/************* TabDos C++ Program Source Code File (.CPP) **************/
/* PROGRAM NAME: TABDOS                                                */
/* -------------                                                       */
/*  Version 4.9                                                        */
/*                                                                     */
/* COPYRIGHT:                                                          */
/* ----------                                                          */
/*  (C) Copyright to the author Olivier BERTRAND          1998-2014    */
/*                                                                     */
/* WHAT THIS PROGRAM DOES:                                             */
/* -----------------------                                             */
/*  This program are the DOS tables classes.                           */
/*                                                                     */
/***********************************************************************/

/***********************************************************************/
/*  Include relevant sections of the System header files.              */
/***********************************************************************/
#include "my_global.h"
#if defined(WIN32)
#include <io.h>
#include <sys\timeb.h>                   // For testing only
#include <fcntl.h>
#include <errno.h>
#if defined(__BORLANDC__)
#define __MFC_COMPAT__                   // To define min/max as macro
#endif   // __BORLANDC__
//#include <windows.h>
#else   // !WIN32
#if defined(UNIX)
#include <errno.h>
#include <unistd.h>
#else   // !UNIX
#include <io.h>
#endif  // !UNIX
#include <fcntl.h>
#endif  // !WIN32

/***********************************************************************/
/*  Include application header files:                                  */
/*  global.h    is header containing all global declarations.          */
/*  plgdbsem.h  is header containing the DB application declarations.  */
/*  filamtxt.h  is header containing the file AM classes declarations. */
/***********************************************************************/
#include "global.h"
#include "osutil.h"
#include "plgdbsem.h"
#include "catalog.h"
#include "mycat.h"
#include "xindex.h"
#include "filamap.h"
#include "filamfix.h"
#include "filamdbf.h"
#if defined(ZIP_SUPPORT)
#include "filamzip.h"
#endif   // ZIP_SUPPORT
#include "tabdos.h"
#include "tabfix.h"
#include "tabmul.h"

/***********************************************************************/
/*  DB static variables.                                               */
/***********************************************************************/
int num_read, num_there, num_eq[2];                 // Statistics
extern "C" int  trace;

/* --------------------------- Class DOSDEF -------------------------- */

/***********************************************************************/
/*  Constructor.                                                       */
/***********************************************************************/
DOSDEF::DOSDEF(void)
  {
  Pseudo = 3;
  Fn = NULL;
  Ofn = NULL;
  To_Indx = NULL;
  Recfm = RECFM_VAR;
  Mapped = false;
  Padded = false;
  Huge = false;
  Accept = false;
  Eof = false;
  Compressed = 0;
  Lrecl = 0;
  AvgLen = 0;
  Block = 0;
  Last = 0;
  Blksize = 0;
  Maxerr = 0;
  ReadMode = 0;
  Ending = 0;
//Mtime = 0;
  } // end of DOSDEF constructor

/***********************************************************************/
/*  DefineAM: define specific AM block values from XDB file.           */
/***********************************************************************/
bool DOSDEF::DefineAM(PGLOBAL g, LPCSTR am, int poff)
  {
  char   buf[8];
  bool   map = (am && (*am == 'M' || *am == 'm'));
  LPCSTR dfm = (am && (*am == 'F' || *am == 'f')) ? "F"
             : (am && (*am == 'B' || *am == 'b')) ? "B"
             : (am && !stricmp(am, "DBF"))        ? "D" : "V";

  Desc = Fn = GetStringCatInfo(g, "Filename", NULL);
  Ofn = GetStringCatInfo(g, "Optname", Fn);
  GetCharCatInfo("Recfm", (PSZ)dfm, buf, sizeof(buf));
  Recfm = (toupper(*buf) == 'F') ? RECFM_FIX :
          (toupper(*buf) == 'B') ? RECFM_BIN :
          (toupper(*buf) == 'D') ? RECFM_DBF : RECFM_VAR;
  Lrecl = GetIntCatInfo("Lrecl", 0);

  if (Recfm != RECFM_DBF)
    Compressed = GetIntCatInfo("Compressed", 0);

  Mapped = GetBoolCatInfo("Mapped", map);
  Block = GetIntCatInfo("Blocks", 0);
  Last = GetIntCatInfo("Last", 0);
  Ending = GetIntCatInfo("Ending", CRLF);

  if (Recfm == RECFM_FIX || Recfm == RECFM_BIN) {
    Huge = GetBoolCatInfo("Huge", Cat->GetDefHuge());
    Padded = GetBoolCatInfo("Padded", false);
    Blksize = GetIntCatInfo("Blksize", 0);
    Eof = (GetIntCatInfo("EOF", 0) != 0);
  } else if (Recfm == RECFM_DBF) {
    Maxerr = GetIntCatInfo("Maxerr", 0);
    Accept = (GetIntCatInfo("Accept", 0) != 0);
    ReadMode = GetIntCatInfo("Readmode", 0);
  } else // (Recfm == RECFM_VAR)
    AvgLen = GetIntCatInfo("Avglen", 0);

  // Ignore wrong Index definitions for catalog commands
  SetIndexInfo();
  return false;
  } // end of DefineAM

#if 0
/***********************************************************************/
/*  DeleteTableFile: Delete DOS/UNIX table files using platform API.   */
/*  If the table file is protected (declared as read/only) we still    */
/*  erase the the eventual optimize and index files but return true.   */
/***********************************************************************/
bool DOSDEF::DeleteTableFile(PGLOBAL g)
  {
  char    filename[_MAX_PATH];
  bool    rc = false;

  // Now delete the table file itself if not protected
  if (!IsReadOnly()) {
    rc = Erase(filename);
  } else
    rc =true;

  return rc;                               // Return true if error
  } // end of DeleteTableFile

/***********************************************************************/
/*  Erase: This was made a separate routine because a strange thing    */
/*  happened when DeleteTablefile was defined for the VCTDEF class:    */
/*  when called from Catalog, the DOSDEF routine was still called even */
/*  when the class was VCTDEF. It also minimizes the specific code.    */
/***********************************************************************/
bool DOSDEF::Erase(char *filename)
  {
  bool rc;

  PlugSetPath(filename, Fn, GetPath());
#if defined(WIN32)
  rc = !DeleteFile(filename);
#else    // UNIX
  rc = remove(filename);
#endif   // UNIX

  return rc;                                  // Return true if error
  } // end of Erase
#endif // 0

/***********************************************************************/
/*  DeleteIndexFile: Delete DOS/UNIX index file(s) using platform API. */
/***********************************************************************/
bool DOSDEF::DeleteIndexFile(PGLOBAL g, PIXDEF pxdf)
  {
  char   *ftype;
  char    filename[_MAX_PATH];
  bool    sep, rc = false;

  if (!To_Indx)
    return false;           // No index

  // If true indexes are in separate files
  sep = GetBoolCatInfo("SepIndex", false); 

  if (!sep && pxdf) {
    strcpy(g->Message, MSG(NO_RECOV_SPACE));
    return true;
    } // endif sep

  switch (Recfm) {
    case RECFM_VAR: ftype = ".dnx"; break;
    case RECFM_FIX: ftype = ".fnx"; break;
    case RECFM_BIN: ftype = ".bnx"; break;
    case RECFM_VCT: ftype = ".vnx"; break;
    case RECFM_DBF: ftype = ".dbx"; break;
    default:
      sprintf(g->Message, MSG(BAD_RECFM_VAL), Recfm);
      return true;
    } // endswitch Ftype

  /*********************************************************************/
  /*  Check for existence of an index file.                            */
  /*********************************************************************/
  if (sep) {
    // Indexes are save in separate files
#if !defined(UNIX)
    char drive[_MAX_DRIVE];
#else
    char *drive = NULL;
#endif
    char direc[_MAX_DIR];
    char fname[_MAX_FNAME];

    for (; pxdf; pxdf = pxdf->GetNext()) {
      _splitpath(Ofn, drive, direc, fname, NULL);
      strcat(strcat(fname, "_"), pxdf->GetName());
      _makepath(filename, drive, direc, fname, ftype);
      PlugSetPath(filename, filename, GetPath());
#if defined(WIN32)
      rc |= !DeleteFile(filename);
#else    // UNIX
      rc |= remove(filename);
#endif   // UNIX
      } // endfor pxdf

  } else {  // !sep
    // Drop all indexes, delete the common file
    PlugSetPath(filename, Ofn, GetPath());
    strcat(PlugRemoveType(filename, filename), ftype);
#if defined(WIN32)
    rc = !DeleteFile(filename);
#else    // UNIX
    rc = remove(filename);
#endif   // UNIX
  } // endif sep

  if (rc)
    sprintf(g->Message, MSG(DEL_FILE_ERR), filename);

  return rc;                        // Return true if error
  } // end of DeleteIndexFile

/***********************************************************************/
/*  InvalidateIndex: mark all indexes as invalid.                      */
/***********************************************************************/
bool DOSDEF::InvalidateIndex(PGLOBAL g)
  {
  if (To_Indx)
    for (PIXDEF xp = To_Indx; xp; xp = xp->Next)
      xp->Invalid = true;

  return false;
  } // end of InvalidateIndex

/***********************************************************************/
/*  GetTable: makes a new Table Description Block.                     */
/***********************************************************************/
PTDB DOSDEF::GetTable(PGLOBAL g, MODE mode)
  {
  // Mapping not used for insert
  USETEMP tmp = PlgGetUser(g)->UseTemp;
  bool    map = Mapped && mode != MODE_INSERT &&
                !(tmp != TMP_NO && Recfm == RECFM_VAR
                                && mode == MODE_UPDATE) &&
                !(tmp == TMP_FORCE &&
                (mode == MODE_UPDATE || mode == MODE_DELETE));
  PTXF    txfp;
  PTDBASE tdbp;

  /*********************************************************************/
  /*  Allocate table and file processing class of the proper type.     */
  /*  Column blocks will be allocated only when needed.                */
  /*********************************************************************/
  if (Recfm == RECFM_DBF) {
    if (Catfunc == FNC_NO) {
      if (map)
        txfp = new(g) DBMFAM(this);
      else
        txfp = new(g) DBFFAM(this);

      tdbp = new(g) TDBFIX(this, txfp);
    } else                   // Catfunc should be 'C'
      tdbp = new(g) TDBDCL(this);

  } else if (Recfm != RECFM_VAR && Compressed < 2) {
    if (Huge)
      txfp = new(g) BGXFAM(this);
    else if (map)
      txfp = new(g) MPXFAM(this);
    else if (Compressed) {
#if defined(ZIP_SUPPORT)
      txfp = new(g) ZIXFAM(this);
#else   // !ZIP_SUPPORT
      sprintf(g->Message, MSG(NO_FEAT_SUPPORT), "ZIP");
      return NULL;
#endif  // !ZIP_SUPPORT
    } else
      txfp = new(g) FIXFAM(this);

    tdbp = new(g) TDBFIX(this, txfp);
  } else {
    if (Compressed) {
#if defined(ZIP_SUPPORT)
      if (Compressed == 1)
        txfp = new(g) ZIPFAM(this);
      else {
        strcpy(g->Message, "Compress 2 not supported yet");
        return NULL;
      } // endelse
#else   // !ZIP_SUPPORT
      sprintf(g->Message, MSG(NO_FEAT_SUPPORT), "ZIP");
      return NULL;
#endif  // !ZIP_SUPPORT
    } else if (map)
      txfp = new(g) MAPFAM(this);
    else
      txfp = new(g) DOSFAM(this);

    // Txfp must be set even for not multiple tables because
    // it is needed when calling Cardinality in GetBlockValues.
    tdbp = new(g) TDBDOS(this, txfp);
  } // endif Recfm

  if (Multiple)
    tdbp = new(g) TDBMUL(tdbp);

  return tdbp;
  } // end of GetTable

/* ------------------------ Class TDBDOS ----------------------------- */

/***********************************************************************/
/*  Implementation of the TDBDOS class. This is the common class that  */
/*  contain all that is common between the TDBDOS and TDBMAP classes.  */
/***********************************************************************/
TDBDOS::TDBDOS(PDOSDEF tdp, PTXF txfp) : TDBASE(tdp)
  {
  if ((Txfp = txfp))
    Txfp->SetTdbp(this);

  Lrecl = tdp->Lrecl;
  AvgLen = tdp->AvgLen;
  Ftype = tdp->Recfm;
  To_Line = NULL;
  Cardinal = -1;
  } // end of TDBDOS standard constructor

TDBDOS::TDBDOS(PGLOBAL g, PTDBDOS tdbp) : TDBASE(tdbp)
  {
  Txfp = (g) ? tdbp->Txfp->Duplicate(g) : tdbp->Txfp;
  Lrecl = tdbp->Lrecl;
  AvgLen = tdbp->AvgLen;
  Ftype = tdbp->Ftype;
  To_Line = tdbp->To_Line;
  Cardinal = tdbp->Cardinal;
  } // end of TDBDOS copy constructor

// Method
PTDB TDBDOS::CopyOne(PTABS t)
  {
  PTDB    tp;
  PDOSCOL cp1, cp2;
  PGLOBAL g = t->G;

  tp = new(g) TDBDOS(g, this);

  for (cp1 = (PDOSCOL)Columns; cp1; cp1 = (PDOSCOL)cp1->GetNext()) {
    cp2 = new(g) DOSCOL(cp1, tp);  // Make a copy
    NewPointer(t, cp1, cp2);
    } // endfor cp1

  return tp;
  } // end of CopyOne

/***********************************************************************/
/*  Allocate DOS column description block.                             */
/***********************************************************************/
PCOL TDBDOS::MakeCol(PGLOBAL g, PCOLDEF cdp, PCOL cprec, int n)
  {
  return new(g) DOSCOL(g, cdp, this, cprec, n);
  } // end of MakeCol

/***********************************************************************/
/*  Print debug information.                                           */
/***********************************************************************/
void TDBDOS::PrintAM(FILE *f, char *m)
  {
  fprintf(f, "%s AM(%d): mode=%d\n", m, GetAmType(), Mode);

  if (Txfp->To_File)
    fprintf(f, "%s  File: %s\n", m, Txfp->To_File);

  } // end of PrintAM

/***********************************************************************/
/*  Remake the indexes after the table was modified.                   */
/***********************************************************************/
int TDBDOS::ResetTableOpt(PGLOBAL g, bool dox)
  {
  int  rc = RC_OK;

  MaxSize = -1;                        // Size must be recalculated
  Cardinal = -1;                       // as well as Cardinality

  if (dox) {
    // Remake eventual indexes
    if (Mode != MODE_UPDATE)
      To_SetCols = NULL;                // Only used on Update

    Columns = NULL;                     // Not used anymore
    Txfp->Reset();                      // New start
    Use = USE_READY;                    // So the table can be reopened
    Mode = MODE_READ;                   // New mode

    if (!(PlgGetUser(g)->Check & CHK_OPT)) {
      // After the table was modified the indexes
      // are invalid and we should mark them as such...
      rc = ((PDOSDEF)To_Def)->InvalidateIndex(g);
    } else
      // ... or we should remake them.
      rc = MakeIndex(g, NULL, false);

    } // endif dox

  return rc;
  } // end of ResetTableOpt

/***********************************************************************/
/*  Check whether we have to create/update permanent indexes.          */
/***********************************************************************/
int TDBDOS::MakeIndex(PGLOBAL g, PIXDEF pxdf, bool add)
  {
  int     k, n;
  bool    fixed, doit, sep, b = (pxdf != NULL);
  PCOL   *keycols, colp;
  PIXDEF  xdp, sxp = NULL;
  PKPDEF  kdp;
  PDOSDEF dfp;
//PCOLDEF cdp;
  PXINDEX x;
  PXLOAD  pxp;

  Mode = MODE_READ;
  Use = USE_READY;
  dfp = (PDOSDEF)To_Def;
  fixed = Cardinality(g) >= 0;

  // Are we are called from CreateTable or CreateIndex?
  if (pxdf) {
    if (!add && dfp->GetIndx()) {
      strcpy(g->Message, MSG(INDX_EXIST_YET));
      return RC_FX;
      } // endif To_Indx

    if (add && dfp->GetIndx()) {
      for (sxp = dfp->GetIndx(); sxp; sxp = sxp->GetNext())
        if (!stricmp(sxp->GetName(), pxdf->GetName())) {
          sprintf(g->Message, MSG(INDEX_YET_ON), pxdf->GetName(), Name);
          return RC_FX;
        } else if (!sxp->GetNext())
          break;

      sxp->SetNext(pxdf);
//    first = false;
    } else
      dfp->SetIndx(pxdf);

//  pxdf->SetDef(dfp);
  } else if (!(pxdf = dfp->GetIndx()))
    return RC_INFO;              // No index to make

  // Allocate all columns that will be used by indexes.
  // This must be done before opening the table so specific
  // column initialization can be done ( in particular by TDBVCT)
  for (n = 0, xdp = pxdf; xdp; xdp = xdp->GetNext())
    for (kdp = xdp->GetToKeyParts(); kdp; kdp = kdp->GetNext()) {
      if (!(colp = ColDB(g, kdp->GetName(), 0))) {
        sprintf(g->Message, MSG(INDX_COL_NOTIN), kdp->GetName(), Name);
        goto err;
      } else if (colp->GetResultType() == TYPE_DECIM) {
        sprintf(g->Message, "Decimal columns are not indexable yet");
        goto err;
      } // endif Type

      colp->InitValue(g);
      n = MY_MAX(n, xdp->GetNparts());
      } // endfor kdp

  keycols = (PCOL*)PlugSubAlloc(g, NULL, n * sizeof(PCOL));
  sep = dfp->GetBoolCatInfo("SepIndex", false);

  /*********************************************************************/
  /*  Construct and save the defined indexes.                          */
  /*********************************************************************/
  for (xdp = pxdf; xdp; xdp = xdp->GetNext())
    if (!OpenDB(g)) {
      if (xdp->IsAuto() && fixed)
        // Auto increment key and fixed file: use an XXROW index
        continue;      // XXROW index doesn't need to be made

      // On Update, redo only indexes that are modified
      doit = !To_SetCols;
      n = 0;

      if (sxp)
        xdp->SetID(sxp->GetID() + 1);

      for (kdp = xdp->GetToKeyParts(); kdp; kdp = kdp->GetNext()) {
        // Check whether this column was updated
        for (colp = To_SetCols; !doit && colp; colp = colp->GetNext())
          if (!stricmp(kdp->GetName(), colp->GetName()))
            doit = true;

        keycols[n++] = ColDB(g, kdp->GetName(), 0);
        } // endfor kdp

      // If no indexed columns were updated, don't remake the index
      // if indexes are in separate files.
      if (!doit && sep)
        continue;

      k = xdp->GetNparts();

      // Make the index and save it
      if (dfp->Huge)
        pxp = new(g) XHUGE;
      else
        pxp = new(g) XFILE;

      if (k == 1)            // Simple index
        x = new(g) XINDXS(this, xdp, pxp, keycols);
      else                   // Multi-Column index
        x = new(g) XINDEX(this, xdp, pxp, keycols);

      if (!x->Make(g, sxp)) {
        // Retreive define values from the index
        xdp->SetMaxSame(x->GetMaxSame());
//      xdp->SetSize(x->GetSize());

        // store KXYCOL Mxs in KPARTDEF Mxsame
        xdp->SetMxsame(x);

#if defined(TRACE)
        printf("Make done...\n");
#endif   // TRACE

//      if (x->GetSize() > 0)
          sxp = xdp;

        xdp->SetInvalid(false);
      } else
        goto err;

    } else
      return RC_INFO;     // Error or Physical table does not exist

  if (Use == USE_OPEN)
    CloseDB(g);

  return RC_OK;

err:
  if (sxp)
    sxp->SetNext(NULL);
  else
    dfp->SetIndx(NULL);

  return RC_FX;
  } // end of MakeIndex

/***********************************************************************/
/*  DOS GetProgMax: get the max value for progress information.        */
/***********************************************************************/
int TDBDOS::GetProgMax(PGLOBAL g)
  {
  return (To_Kindex) ? GetMaxSize(g) : GetFileLength(g);
  } // end of GetProgMax

/***********************************************************************/
/*  DOS GetProgCur: get the current value for progress information.    */
/***********************************************************************/
int TDBDOS::GetProgCur(void)
  {
  return (To_Kindex) ? To_Kindex->GetCur_K() + 1 : GetRecpos();
  } // end of GetProgCur

/***********************************************************************/
/*  RowNumber: return the ordinal number of the current row.           */
/***********************************************************************/
int TDBDOS::RowNumber(PGLOBAL g, bool b)
  {
  if (To_Kindex) {
    /*******************************************************************/
    /*  Don't know how to retrieve RowID from file address.            */
    /*******************************************************************/
    sprintf(g->Message, MSG(NO_ROWID_FOR_AM),
                        GetAmName(g, Txfp->GetAmType()));
    return 0;
  } else
    return Txfp->GetRowID();

  } // end of RowNumber

/***********************************************************************/
/*  DOS Cardinality: returns table cardinality in number of rows.      */
/*  This function can be called with a null argument to test the       */
/*  availability of Cardinality implementation (1 yes, 0 no).          */
/***********************************************************************/
int TDBDOS::Cardinality(PGLOBAL g)
  {
  if (!g)
    return Txfp->Cardinality(g);

  if (Cardinal < 0)
    Cardinal = Txfp->Cardinality(g);

  return Cardinal;
  } // end of Cardinality

/***********************************************************************/
/*  DOS GetMaxSize: returns file size estimate in number of lines.     */
/*  This function covers variable record length files.                 */
/***********************************************************************/
int TDBDOS::GetMaxSize(PGLOBAL g)
  {
  if (MaxSize >= 0)
    return MaxSize;

  if (!Cardinality(NULL)) {
    int len = GetFileLength(g);

    if (len >= 0) {
      if (trace)
        htrc("Estimating lines len=%d ending=%d\n",
              len, ((PDOSDEF)To_Def)->Ending);

      /*****************************************************************/
      /*  Estimate the number of lines in the table (if not known) by  */
      /*  dividing the file length by the minimum line length assuming */
      /*  only the last column can be of variable length. This will be */
      /*  a ceiling estimate (as last column is never totally absent). */
      /*****************************************************************/
      int rec = ((PDOSDEF)To_Def)->Ending;     // +2: CRLF +1: LF

      if (AvgLen <= 0)     // No given average estimate
        rec += EstimatedLength(g);
      else   // A lower estimate was given for the average record length
        rec += (int)AvgLen;

      if (trace)
        htrc(" Filen=%d min_rec=%d\n", len, rec);
              
      MaxSize = (len + rec - 1) / rec;

      if (trace)
        htrc(" Estimated max_K=%d\n", MaxSize);

      } // endif len

  } else
    MaxSize = Cardinality(g);

  return MaxSize;
  } // end of GetMaxSize

/***********************************************************************/
/*  DOS EstimatedLength. Returns an estimated minimum line length.     */
/***********************************************************************/
int TDBDOS::EstimatedLength(PGLOBAL g)
  {
  int     dep = 0;
  PCOLDEF cdp = To_Def->GetCols();

  if (!cdp->GetNext()) {
    // One column table, we are going to return a ridiculous
    // result if we set dep to 1
    dep = 1 + cdp->GetLong() / 20;           // Why 20 ?????
  } else for (; cdp; cdp = cdp->GetNext())
    dep = MY_MAX(dep, cdp->GetOffset());

  return (int)dep;
  } // end of Estimated Length

/***********************************************************************/
/*  DOS tables favor the use temporary files for Update.               */
/***********************************************************************/
bool TDBDOS::IsUsingTemp(PGLOBAL g)
  {
  USETEMP usetemp = PlgGetUser(g)->UseTemp;

  return (usetemp == TMP_YES || usetemp == TMP_FORCE ||
         (usetemp == TMP_AUTO && Mode == MODE_UPDATE));
  } // end of IsUsingTemp

/***********************************************************************/
/*  DOS Access Method opening routine.                                 */
/*  New method now that this routine is called recursively (last table */
/*  first in reverse order): index blocks are immediately linked to    */
/*  join block of next table if it exists or else are discarted.       */
/***********************************************************************/
bool TDBDOS::OpenDB(PGLOBAL g)
  {
  if (trace)
    htrc("DOS OpenDB: tdbp=%p tdb=R%d use=%d mode=%d\n",
          this, Tdb_No, Use, Mode);

  if (Use == USE_OPEN) {
    /*******************************************************************/
    /*  Table already open, just replace it at its beginning.          */
    /*******************************************************************/
    if (!To_Kindex) {
      Txfp->Rewind();       // see comment in Work.log

      if (SkipHeader(g))
        return TRUE;

    } else
      /*****************************************************************/
      /*  Table is to be accessed through a sorted index table.        */
      /*****************************************************************/
      To_Kindex->Reset();

    return false;
    } // endif use

  if (Mode == MODE_DELETE && !Next && Txfp->GetAmType() != TYPE_AM_DOS) {
    // Delete all lines. Not handled in MAP or block mode
    Txfp = new(g) DOSFAM((PDOSDEF)To_Def);
    Txfp->SetTdbp(this);
  } else if (Txfp->Blocked && (Mode == MODE_DELETE ||
      (Mode == MODE_UPDATE && PlgGetUser(g)->UseTemp != TMP_NO))) {
    /*******************************************************************/
    /*  Delete is not currently handled in block mode neither Update   */
    /*  when using a temporary file.                                   */
    /*******************************************************************/
    if (Txfp->GetAmType() == TYPE_AM_MAP && Mode == MODE_DELETE)
      Txfp = new(g) MAPFAM((PDOSDEF)To_Def);
#if defined(ZIP_SUPPORT)
    else if (Txfp->GetAmType() == TYPE_AM_ZIP)
      Txfp = new(g) ZIPFAM((PDOSDEF)To_Def);
#endif   // ZIP_SUPPORT
    else if (Txfp->GetAmType() != TYPE_AM_DOS)
      Txfp = new(g) DOSFAM((PDOSDEF)To_Def);

    Txfp->SetTdbp(this);
    } // endif Mode

  /*********************************************************************/
  /*  Open according to logical input/output mode required.            */
  /*  Use conventionnal input/output functions.                        */
  /*  Treat files as binary in Delete mode (for line moving)           */
  /*********************************************************************/
  if (Txfp->OpenTableFile(g))
    return true;

  Use = USE_OPEN;       // Do it now in case we are recursively called

  /*********************************************************************/
  /*  Allocate the line buffer plus a null character.                  */
  /*********************************************************************/
  To_Line = (char*)PlugSubAlloc(g, NULL, Lrecl + 1);

  if (Mode == MODE_INSERT) {
    // Spaces between fields must be filled with blanks
    memset(To_Line, ' ', Lrecl);
    To_Line[Lrecl] = '\0';
  } else
    memset(To_Line, 0, Lrecl + 1);

  if (trace)
    htrc("OpenDos: R%hd mode=%d To_Line=%p\n", Tdb_No, Mode, To_Line);

  if (SkipHeader(g))         // When called from CSV/FMT files
    return true;

  /*********************************************************************/
  /*  Reset statistics values.                                         */
  /*********************************************************************/
  num_read = num_there = num_eq[0] = num_eq[1] = 0;
  return false;
  } // end of OpenDB

/***********************************************************************/
/*  ReadDB: Data Base read routine for DOS access method.              */
/***********************************************************************/
int TDBDOS::ReadDB(PGLOBAL g)
  {
  if (trace > 1)
    htrc("DOS ReadDB: R%d Mode=%d key=%p link=%p Kindex=%p To_Line=%p\n",
          GetTdb_No(), Mode, To_Key_Col, To_Link, To_Kindex, To_Line);

  if (To_Kindex) {
    /*******************************************************************/
    /*  Reading is by an index table.                                  */
    /*******************************************************************/
    int recpos = To_Kindex->Fetch(g);

    switch (recpos) {
      case -1:           // End of file reached
        return RC_EF;
      case -2:           // No match for join
        return RC_NF;
      case -3:           // Same record as last non null one
        num_there++;
        return RC_OK;
      default:
        /***************************************************************/
        /*  Set the file position according to record to read.         */
        /***************************************************************/
        if (SetRecpos(g, recpos))
          return RC_FX;

        if (trace > 1)
          htrc("File position is now %d\n", GetRecpos());

        if (Mode == MODE_READ)
          /*************************************************************/
          /*  Defer physical reading until one column setting needs it */
          /*  as it can be a big saving on joins where no other column */
          /*  than the keys are used, so reading is unnecessary.       */
          /*************************************************************/
          if (Txfp->DeferReading())
            return RC_OK;

      } // endswitch recpos

    } // endif To_Kindex

  if (trace > 1)
    htrc(" ReadDB: this=%p To_Line=%p\n", this, To_Line);

  /*********************************************************************/
  /*  Now start the reading process.                                   */
  /*********************************************************************/
  return ReadBuffer(g);
  } // end of ReadDB

/***********************************************************************/
/*  WriteDB: Data Base write routine for DOS access method.            */
/***********************************************************************/
int TDBDOS::WriteDB(PGLOBAL g)
  {
  if (trace > 1)
    htrc("DOS WriteDB: R%d Mode=%d \n", Tdb_No, Mode);

  if (!Ftype && (Mode == MODE_INSERT || Txfp->GetUseTemp())) {
    char *p;

    /*******************************************************************/
    /*  Suppress trailing blanks.                                      */
    /*  Also suppress eventual null from last line.                    */
    /*******************************************************************/
    for (p = To_Line + Lrecl -1; p >= To_Line; p--)
      if (*p && *p != ' ')
        break;

    *(++p) = '\0';
    } // endif Mode

  if (trace > 1)
    htrc("Write: line is='%s'\n", To_Line);

  // Now start the writing process
  return Txfp->WriteBuffer(g);
  } // end of WriteDB

/***********************************************************************/
/*  Data Base delete line routine for DOS (and FIX) access method.     */
/*  RC_FX means delete all. Nothing to do here (was done at open).     */
/***********************************************************************/
int TDBDOS::DeleteDB(PGLOBAL g, int irc)
  {
    return (irc == RC_FX) ? RC_OK : Txfp->DeleteRecords(g, irc);
  } // end of DeleteDB

/***********************************************************************/
/*  Data Base close routine for DOS access method.                     */
/***********************************************************************/
void TDBDOS::CloseDB(PGLOBAL g)
  {
  if (To_Kindex) {
    To_Kindex->Close();
    To_Kindex = NULL;
    } // endif

  Txfp->CloseTableFile(g);
  } // end of CloseDB

// ------------------------ DOSCOL functions ----------------------------

/***********************************************************************/
/*  DOSCOL public constructor (also called by MAPCOL).                 */
/***********************************************************************/
DOSCOL::DOSCOL(PGLOBAL g, PCOLDEF cdp, PTDB tp, PCOL cp, int i, PSZ am)
  : COLBLK(cdp, tp, i)
  {
  char *p;
  int   prec = Format.Prec;
  PTXF  txfp = ((PTDBDOS)tp)->Txfp;

  assert(cdp);

  if (cp) {
    Next = cp->GetNext();
    cp->SetNext(this);
  } else {
    Next = tp->GetColumns();
    tp->SetColumns(this);
  } // endif cprec

  // Set additional Dos access method information for column.
  Deplac = cdp->GetOffset();
  Long = cdp->GetLong();
  To_Val = NULL;
  OldVal = NULL;                  // Currently used only in MinMax
  Ldz = false;
  Nod = false;
  Dcm = -1;
  p = cdp->GetFmt();

  if (p && IsTypeNum(Buf_Type)) {
    // Formatted numeric value
    for (; p && *p && isalpha(*p); p++)
      switch (toupper(*p)) {
        case 'Z':                 // Have leading zeros
          Ldz = true;
          break;
        case 'N':                 // Have no decimal point
          Nod = true;
          break;
        } // endswitch p

    // Set number of decimal digits
    Dcm = (*p) ? atoi(p) : GetScale();
    } // endif fmt

  if (trace)
    htrc(" making new %sCOL C%d %s at %p\n", am, Index, Name, this);

  } // end of DOSCOL constructor

/***********************************************************************/
/*  DOSCOL constructor used for copying columns.                       */
/*  tdbp is the pointer to the new table descriptor.                   */
/***********************************************************************/
DOSCOL::DOSCOL(DOSCOL *col1, PTDB tdbp) : COLBLK(col1, tdbp)
  {
  Deplac = col1->Deplac;
  Long = col1->Long;
  To_Val = col1->To_Val;
  Ldz = col1->Ldz;
  Nod = col1->Nod;
  Dcm = col1->Dcm;
  OldVal = col1->OldVal;
  Buf = col1->Buf;
  } // end of DOSCOL copy constructor

/***********************************************************************/
/*  SetBuffer: prepare a column block for write operation.             */
/***********************************************************************/
bool DOSCOL::SetBuffer(PGLOBAL g, PVAL value, bool ok, bool check)
  {
  if (!(To_Val = value)) {
    sprintf(g->Message, MSG(VALUE_ERROR), Name);
    return true;
  } else if (Buf_Type == value->GetType()) {
    // Values are of the (good) column type
    if (Buf_Type == TYPE_DATE) {
      // If any of the date values is formatted
      // output format must be set for the receiving table
      if (GetDomain() || ((DTVAL *)value)->IsFormatted())
        goto newval;          // This will make a new value;

    } else if (Buf_Type == TYPE_DOUBLE)
      // Float values must be written with the correct (column) precision
      // Note: maybe this should be forced by ShowValue instead of this ?
      value->SetPrec(GetScale());

    Value = value;            // Directly access the external value
  } else {
    // Values are not of the (good) column type
    if (check) {
      sprintf(g->Message, MSG(TYPE_VALUE_ERR), Name,
              GetTypeName(Buf_Type), GetTypeName(value->GetType()));
      return true;
      } // endif check

 newval:
    if (InitValue(g))         // Allocate the matching value block
      return true;

  } // endif's Value, Buf_Type

  // Allocate the buffer used in WriteColumn for numeric columns
  if (IsTypeNum(Buf_Type))
    Buf = (char*)PlugSubAlloc(g, NULL, MY_MAX(32, Long + Dcm + 1));

  // Because Colblk's have been made from a copy of the original TDB in
  // case of Update, we must reset them to point to the original one.
  if (To_Tdb->GetOrig())
    To_Tdb = (PTDB)To_Tdb->GetOrig();

  // Set the Column
  Status = (ok) ? BUF_EMPTY : BUF_NO;
  return false;
  } // end of SetBuffer

/***********************************************************************/
/*  ReadColumn: what this routine does is to access the last line      */
/*  read from the corresponding table, extract from it the field       */
/*  corresponding to this column and convert it to buffer type.        */
/***********************************************************************/
void DOSCOL::ReadColumn(PGLOBAL g)
  {
  char   *p = NULL;
  int     i, rc;
  int     field;
  double  dval;
  PTDBDOS tdbp = (PTDBDOS)To_Tdb;

  if (trace > 1)
    htrc(
      "DOS ReadColumn: col %s R%d coluse=%.4X status=%.4X buf_type=%d\n",
         Name, tdbp->GetTdb_No(), ColUse, Status, Buf_Type);

  /*********************************************************************/
  /*  If physical reading of the line was deferred, do it now.         */
  /*********************************************************************/
  if (!tdbp->IsRead())
    if ((rc = tdbp->ReadBuffer(g)) != RC_OK) {
      if (rc == RC_EF)
        sprintf(g->Message, MSG(INV_DEF_READ), rc);

      longjmp(g->jumper[g->jump_level], 11);
      } // endif

  p = tdbp->To_Line + Deplac;
  field = Long;

  switch (tdbp->Ftype) {
    case RECFM_VAR:
      /*****************************************************************/
      /*  For a variable length file, check if the field exists.       */
      /*****************************************************************/
      if (strlen(tdbp->To_Line) < (unsigned)Deplac)
        field = 0;

    case RECFM_FIX:            // Fixed length text file
    case RECFM_DBF:            // Fixed length DBase file
      if (Nod) switch (Buf_Type) {
        case TYPE_INT:
        case TYPE_SHORT:
        case TYPE_TINY:
        case TYPE_BIGINT:
          if (Value->SetValue_char(p, field - Dcm)) {
            sprintf(g->Message, "Out of range value for column %s at row %d",
                    Name, tdbp->RowNumber(g));
            PushWarning(g, tdbp);
            } // endif SetValue_char

          break;
        case TYPE_DOUBLE:
          Value->SetValue_char(p, field);
          dval = Value->GetFloatValue();

          for (i = 0; i < Dcm; i++)
            dval /= 10.0;

          Value->SetValue(dval);
          break;
        default:
          Value->SetValue_char(p, field);
          break;
        } // endswitch Buf_Type

      else
        if (Value->SetValue_char(p, field)) {
          sprintf(g->Message, "Out of range value for column %s at row %d",
                  Name, tdbp->RowNumber(g));
          PushWarning(g, tdbp);
          } // endif SetValue_char

      break;
    default:
      sprintf(g->Message, MSG(BAD_RECFM), tdbp->Ftype);
      longjmp(g->jumper[g->jump_level], 34);
    } // endswitch Ftype

  // Set null when applicable
  if (Nullable)
    Value->SetNull(Value->IsZero());

  } // end of ReadColumn

/***********************************************************************/
/*  WriteColumn: what this routine does is to access the last line     */
/*  read from the corresponding table, and rewrite the field           */
/*  corresponding to this column from the column buffer and type.      */
/***********************************************************************/
void DOSCOL::WriteColumn(PGLOBAL g)
  {
  char   *p, *p2, fmt[32];
  int     i, k, len, field;
  PTDBDOS tdbp = (PTDBDOS)To_Tdb;

  if (trace > 1)
    htrc("DOS WriteColumn: col %s R%d coluse=%.4X status=%.4X\n",
          Name, tdbp->GetTdb_No(), ColUse, Status);

  p = tdbp->To_Line + Deplac;

  if (trace > 1)
    htrc("Lrecl=%d deplac=%d int=%d\n", tdbp->Lrecl, Deplac, Long);

  field = Long;

  if (tdbp->Ftype == RECFM_VAR && tdbp->Mode == MODE_UPDATE) {
    len = (signed)strlen(tdbp->To_Line);

    if (tdbp->IsUsingTemp(g))
      // Because of eventual missing field(s) the buffer must be reset
      memset(tdbp->To_Line + len, ' ', tdbp->Lrecl - len);
    else
      // The size actually available must be recalculated
      field = MY_MIN(len - Deplac, Long);

    } // endif Ftype

  if (trace > 1)
    htrc("Long=%d field=%d coltype=%d colval=%p\n",
          Long, field, Buf_Type, Value);

  /*********************************************************************/
  /*  Get the string representation of Value according to column type. */
  /*********************************************************************/
  if (Value != To_Val)
    Value->SetValue_pval(To_Val, false);    // Convert the updated value

  /*********************************************************************/
  /*  This test is only useful for compressed(2) tables.               */
  /*********************************************************************/
  if (tdbp->Ftype != RECFM_BIN) {
    if (Ldz || Nod || Dcm >= 0) {
      switch (Buf_Type) {
        case TYPE_SHORT:
          strcpy(fmt, (Ldz) ? "%0*hd" : "%*.hd");
          i = 0;

          if (Nod)
            for (; i < Dcm; i++)
              strcat(fmt, "0");

          len = sprintf(Buf, fmt, field - i, Value->GetShortValue());
          break;
        case TYPE_INT:
          strcpy(fmt, (Ldz) ? "%0*d" : "%*.d");
          i = 0;

          if (Nod)
            for (; i < Dcm; i++)
              strcat(fmt, "0");

          len = sprintf(Buf, fmt, field - i, Value->GetIntValue());
          break;
        case TYPE_TINY:
          strcpy(fmt, (Ldz) ? "%0*d" : "%*.d");
          i = 0;

          if (Nod)
            for (; i < Dcm; i++)
              strcat(fmt, "0");

          len = sprintf(Buf, fmt, field - i, Value->GetTinyValue());
          break;
        case TYPE_DOUBLE:
          strcpy(fmt, (Ldz) ? "%0*.*lf" : "%*.*lf");
          sprintf(Buf, fmt, field + ((Nod && Dcm) ? 1 : 0),
                  Dcm, Value->GetFloatValue());
          len = strlen(Buf);

          if (Nod && Dcm)
            for (i = k = 0; i < len; i++, k++)
              if (Buf[i] != ' ') {
                if (Buf[i] == '.' || Buf[i] == ',')
                  k++;

                Buf[i] = Buf[k];
                } // endif Buf(i)

          len = strlen(Buf);
          break;
        } // endswitch BufType

      p2 = Buf;
    } else                 // Standard PlugDB format
      p2 = Value->ShowValue(Buf, field);

    if (trace)
      htrc("new length(%p)=%d\n", p2, strlen(p2));

    if ((len = strlen(p2)) > field) {
      sprintf(g->Message, MSG(VALUE_TOO_LONG), p2, Name, field);
      longjmp(g->jumper[g->jump_level], 31);
      } // endif

    if (trace > 1)
      htrc("buffer=%s\n", p2);

    /*******************************************************************/
    /*  Updating must be done only when not in checking pass.          */
    /*******************************************************************/
    if (Status) {
      memset(p, ' ', field);
      memcpy(p, p2, len);

      if (trace > 1)
        htrc(" col write: '%.*s'\n", len, p);

      } // endif Use

  } else    // BIN compressed table
    /*******************************************************************/
    /*  Check if updating is Ok, meaning col value is not too long.    */
    /*  Updating to be done only during the second pass (Status=true)  */
    /*******************************************************************/
    if (Value->GetBinValue(p, Long, Status)) {
      sprintf(g->Message, MSG(BIN_F_TOO_LONG),
                          Name, Value->GetSize(), Long);
      longjmp(g->jumper[g->jump_level], 31);
      } // endif

  } // end of WriteColumn

/***********************************************************************/
/*  Make file output of a Dos column descriptor block.                 */
/***********************************************************************/
void DOSCOL::Print(PGLOBAL g, FILE *f, uint n)
  {
  COLBLK::Print(g, f, n);
  } // end of Print

/* ------------------------------------------------------------------- */
