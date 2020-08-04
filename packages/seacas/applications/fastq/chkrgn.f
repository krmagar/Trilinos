C    Copyright(C) 1999-2020 National Technology & Engineering Solutions
C    of Sandia, LLC (NTESS).  Under the terms of Contract DE-NA0003525 with
C    NTESS, the U.S. Government retains certain rights in this software.
C
C    See packages/seacas/LICENSE for details

      SUBROUTINE CHKRGN (IA, L, MP, ML, MS, MR, MSC, N24, IPOINT, COOR,
     &   IPBOUN, ILINE, LTYPE, NINT, FACTOR, LCON, ILBOUN, ISBOUN,
     &   ISIDE, NLPS, IFLINE, ILLIST, IREGN, NSPR, IFSIDE, ISLIST,
     &   NPPF, IFPB, LISTPB, NLPF, IFLB, LISTLB, NSPF, IFSB, LISTSB,
     &   IFHOLE, NHPR, IHLIST, LINKP, LINKL, LINKS, LINKR, LINKSC,
     &   LINKPB, LINKLB, LINKSB, RSIZE, SCHEME, DEFSCH, NPREGN, NPSBC,
     &   NPNODE, MAXNP, MAXNL, MAX3, X, Y, NID, LISTL, NNPS, ANGLE,
     &   MARKED, MXND, MXNPER, MXNL, MAXNBC, MAXSBC, AMESUR, XNOLD,
     &   YNOLD, NXKOLD, MMPOLD, LINKEG, LISTEG, BMESUR, MLINK, NPROLD,
     &   NPNOLD, NPEOLD, NNXK, REMESH, REXMIN, REXMAX, REYMIN, REYMAX,
     &   IDIVIS, SIZMIN, EMAX, EMIN, NOROOM, ERRCHK, ERR)
C***********************************************************************

C  SUBROUTINE CHKRGN - CHECK THAT A REGION MAY BE MESHED

C***********************************************************************

      DIMENSION IA(1)
      DIMENSION IPOINT(MP), COOR(2, MP), IPBOUN(MP)
      DIMENSION ILINE(ML), LTYPE(ML), NINT(ML), FACTOR(ML), LCON(3, ML)
      DIMENSION ILBOUN(ML), ISBOUN(ML), ISIDE(MS), NLPS(MS)
      DIMENSION IFLINE(MS), ILLIST(MS*3)
      DIMENSION IREGN(MR), NSPR(MR), IFSIDE(MR), ISLIST(MR*4)
      DIMENSION SCHEME(MSC), RSIZE (MR)
      DIMENSION NPPF(MP), IFPB(MP), LISTPB(2, MP)
      DIMENSION NLPF(ML), IFLB(ML), LISTLB(2, ML)
      DIMENSION NSPF(ML), IFSB(ML), LISTSB(2, ML)
      DIMENSION LINKP(2, MP), LINKL(2, ML), LINKS(2, MS)
      DIMENSION LINKR(2, MR), LINKSC(2, MR), LINKPB(2, MP)
      DIMENSION LINKLB(2, ML), LINKSB(2, ML)
      DIMENSION X(MAXNP), Y(MAXNP), NID(MAXNP)
      DIMENSION LISTL(MAXNL), MARKED(3, MAXNL)
      DIMENSION NNPS(MAX3), ANGLE(MAXNP)
      DIMENSION IFHOLE(MR), NHPR(MR), IHLIST(MR*2)

      DIMENSION IDUMMY(1)

      DIMENSION AMESUR(NPEOLD), XNOLD(NPNOLD), YNOLD(NPNOLD)
      DIMENSION NXKOLD(NNXK, NPEOLD), MMPOLD(3, NPROLD)
      DIMENSION LINKEG(2, MLINK), LISTEG(4 * NPEOLD), BMESUR(NPNOLD)

      CHARACTER*72 SCHEME, DEFSCH, SCHSTR

      LOGICAL NOROOM, EVEN, ERR, NORM, CCW, REAL, ADDLNK, REMESH
      LOGICAL PENTAG, TRIANG, TRNSIT, HALFC, COUNT, ERRCHK

      ipntr = 0
      addlnk = .false.
      COUNT = .TRUE.
      IF (REMESH) THEN
         EVEN = .TRUE.
      ELSE
         EVEN = .FALSE.
      ENDIF
      REAL = .FALSE.

C  CHECK TO MAKE SURE CONNECTING DATA FOR THE REGION EXISTS
C  AND FILL IN ANY BLANK INTERVALS ACCORDING TO THE GIVEN SIZE
C  FOR THE REGION AND THE LINE'S LENGTH

      CALL DATAOK (MP, ML, MS, MR, L, IREGN(L), COOR, ILINE, LTYPE,
     &   NINT, LCON, NLPS, IFLINE, ILLIST, NSPR, IFSIDE, ISLIST, LINKP,
     &   LINKL, LINKS, RSIZE(L), ERRCHK, ERR)
      IF (ERR) THEN
         WRITE (*, 10000) IREGN(L)
         ADDLNK = .TRUE.
         IMINUS = -L
         CALL LTSORT (MR, LINKR, IREGN(L), IMINUS, ADDLNK)
         ADDLNK = .FALSE.

C  CALCULATE THE PERIMETER OF THE REGION

      ELSE
         KNBC = 0
         KSBC = 0

         CALL PERIM (MP, ML, MS, NSPR(L), MAXNL, MAXNP, 1, 1, KNBC,
     &      KSBC, IREGN(L), IPOINT, COOR, IPBOUN, ILINE, LTYPE, NINT,
     &      FACTOR, LCON, ILBOUN, ISBOUN, ISIDE, NLPS, IFLINE, ILLIST,
     &      ISLIST(IFSIDE(L)), NPPF, IFPB, LISTPB, NLPF, IFLB, LISTLB,
     &      NSPF, IFSB, LISTSB, LINKP, LINKL, LINKS, LINKPB, LINKLB,
     &      LINKSB, X, Y, NID, NPER, LISTL, NL, IDUMMY, MARKED, EVEN,
     &      REAL, ERR, CCW, COUNT, NOROOM, AMESUR, XNOLD, YNOLD, NXKOLD,
     &      MMPOLD, LINKEG, LISTEG, BMESUR, MLINK, NPROLD, NPNOLD,
     &      NPEOLD, NNXK, REMESH, REXMIN, REXMAX, REYMIN, REYMAX,
     &      IDIVIS, SIZMIN, EMAX, EMIN)
         IF ((NPER .LE. 0) .OR. (ERR)) THEN
            WRITE (*, 10010) IREGN(L)
            ADDLNK = .TRUE.
            IMINUS = -L
            CALL LTSORT (MR, LINKR, IREGN(L), IMINUS, ADDLNK)
            ADDLNK = .FALSE.
            GO TO 120
         END IF

C  WHEN CHECKING THE MAXIMUMS - ADD ENOUGH FOR ONE MORE INTERVAL
C  ON THE LINE AS THIS LINE MAY BE INCREMENTED BY ONE IF THE
C  PERIMETER IS ODD

         MAXNBC = MAX(MAXNBC, KNBC + 3)
         MAXSBC = MAX(MAXSBC, KSBC + 3)
         MXNL = MAX(MXNL, NL)

C  GET THE REGION SCHEME

         CALL LTSORT (MR, LINKSC, ABS(IREGN(L)), IPNTR, ADDLNK)
         IF ((IREGN(L) .LE. N24) .AND. (IPNTR .GT. 0)) THEN
            SCHSTR = SCHEME(IPNTR)
         ELSE
            SCHSTR = DEFSCH
         END IF

C  SEE IF A TRIANGULAR, PENTAGON, SEMICIRCLE, OR A TRANSITION
C  REGION HAS BEEN FLAGGED

         PENTAG = .FALSE.
         TRNSIT = .FALSE.
         TRIANG = .FALSE.
         CALL STRCUT (SCHSTR)
         CALL STRLNG (SCHSTR, LENSCH)
         DO 100 J = 1, LENSCH
            IF ((SCHSTR(J:J) .EQ. 'T') .OR.
     &         (SCHSTR(J:J) .EQ. 't')) THEN
               IF (NPER .GE. 6) THEN
                  TRIANG = .TRUE.
               ELSE
                  CALL MESAGE ('TRIANGULAR REGION MESH NOT')
                  CALL MESAGE ('POSSIBLE WITH PERIMETER < 6')
                  CALL MESAGE ('REGULAR PROCESSING ASSUMED')
               END IF
               GO TO 110
            ELSE IF ((SCHSTR(J:J) .EQ. 'U') .OR.
     &         (SCHSTR(J:J) .EQ. 'u')) THEN
               IF (NPER .GE. 10) THEN
                  PENTAG = .TRUE.
               ELSE
                  CALL MESAGE ('PENTAGON REGION MESH NOT')
                  CALL MESAGE ('POSSIBLE WITH PERIMETER < 10')
                  CALL MESAGE ('REGULAR PROCESSING ASSUMED')
               END IF
               GO TO 110
            ELSE IF ((SCHSTR(J:J) .EQ. 'B') .OR.
     &         (SCHSTR(J:J) .EQ. 'b')) THEN
               IF (NPER .GE. 8) THEN
                  TRNSIT = .TRUE.
                  HALFC = .FALSE.
               ELSE
                  CALL MESAGE ('TRANSITION REGION GENERATION NOT')
                  CALL MESAGE ('POSSIBLE WITH NO. IN PERIMETER < 8')
                  CALL MESAGE ('REGULAR PROCESSING WILL BE ATTEMPTED')
               END IF
               GO TO 110
            ELSE IF ((SCHSTR(J:J) .EQ. 'C') .OR.
     &         (SCHSTR(J:J) .EQ. 'c'))
     &         THEN
               IF (NPER .GE. 8) THEN
                  TRNSIT = .TRUE.
                  HALFC = .TRUE.
               ELSE
                  CALL MESAGE ('SEMICIRCLE REGION GENERATION NOT')
                  CALL MESAGE ('POSSIBLE WITH NO. IN PERIMETER < 8')
                  CALL MESAGE ('REGULAR PROCESSING WILL BE ATTEMPTED')
               END IF
               GO TO 110
            END IF
  100    CONTINUE
  110    CONTINUE

C  SET UP THE TRIANGLE DIVISIONS, AND FIND THE CENTER POINT

         IF (TRIANG) THEN
            CALL GETM3 (ML, MS, MAX3, NSPR(L), ISLIST(IFSIDE(L)), NINT,
     &         IFLINE, NLPS, ILLIST, LINKL, LINKS, X, Y, NID, NNPS,
     &         ANGLE, NPER, M1A, M1B, M2A, M2B, M3A, M3B, XCEN, YCEN,
     &         CCW, ERR)

C  CHECK FOR MAXIMUM DIMENSIONS NEEDED FOR EACH REGION
C  ASSUMING THAT 10 NECKLACES WILL BE ADEQUATE

            MXTEST = ((M1A + 1)*(M3B + 1)) + ((M1B + 1)*(M2A + 1))
     &         + ((M2B + 1)*(M3A + 1)) + (10*(NPER + 1)) + (NPER*2)
            MXND = MAX(MXTEST, MXND)
            MXNPER = MAX(MXNPER, (NPER + 2) * 2)

C  SET UP THE TRANSITION DIVISIONS, AND FIND THE CENTER POINT

         ELSE IF (TRNSIT) THEN
            CALL GETTRN (ML, MS, MAX3, NSPR(L), ISLIST(IFSIDE(L)), NINT,
     &         IFLINE, NLPS, ILLIST, LINKL, LINKS, X, Y, NID, NNPS,
     &         ANGLE, NPER, I1, I2, I3, I4, I5, I6, I7, I8, XCEN1,
     &         YCEN1, XCEN2, YCEN2, XMID1, YMID1, XMID2, YMID2, CCW,
     &         HALFC, ERR)

C  CHECK FOR MAXIMUM DIMENSIONS NEEDED FOR EACH REGION
C  ASSUMING THAT 10 NECKLACES WILL BE ADEQUATE

            MXTEST = ((I2 - I1)*(NPER - I8))
     &         + ((I3 - I2)*(NPER - I8)) + ((I3 - I2)*(I2 - I2))
     &         + ((I4 - I3)*(I7 - I6)) + ((I5 - I4)*(I6 - I5))
     &         + ((I5 - I4)*(I7 - I6)) + (10*(NPER + 1)) + (NPER*2)
            MXND = MAX(MXTEST, MXND)
            MXNPER = MAX(MXNPER, (NPER + 2) * 2)

C  SET UP THE PENTAGON DIVISIONS, AND FIND THE CENTER POINT

         ELSE IF (PENTAG) THEN
            CALL GETM5 (IA, ML, MS, MAX3, NSPR(L), ISLIST(IFSIDE(L)),
     &         NINT, IFLINE, NLPS, ILLIST, LINKL, LINKS, X, Y, NID,
     &         NNPS, ANGLE, NPER, M1A, M1B, M2, M3A, M3B, M4A, M4B,
     &         M5, MC, XCEN, YCEN, CCW, ERR)

C  CHECK FOR MAXIMUM DIMENSIONS NEEDED FOR THE REGION
C  ASSUMING THAT 10 NECKLACES WILL BE ADEQUATE

            MXTEST = (M1B*M2) + (M4A*M3B) + (M4B*M5) + (10*(NPER + 1))
            MXND = MAX(MXTEST, MXND)
            MXNPER = MAX(MXNPER, (NPER + 2) *2)

C  CALCULATE THE BASE OF THE RECTANGLE FOR THE REGION

         ELSE
            CALL GETM1 (ML, MS, MAX3, NSPR(L), ISLIST(IFSIDE(L)), NINT,
     &         IFLINE, NLPS, ILLIST, LINKL, LINKS, X, Y, NID, NNPS,
     &         ANGLE, NPER, SCHSTR, M1, CCW, NORM, REAL, ERR)
            IF (ERR) THEN
               WRITE (*, 10020) IREGN(L)
               ADDLNK = .TRUE.
               IMINUS = -L
               CALL LTSORT (MR, LINKR, IREGN(L), IMINUS, ADDLNK)
               ADDLNK = .FALSE.
               GO TO 120
            END IF
            M2 = NPER/2 - M1

C  CHECK FOR MAXIMUM DIMENSIONS NEEDED FOR EACH REGION
C  ASSUMING THAT 10 NECKLACES WILL BE ADEQUATE

            MXTEST = ((M1 + 1)*(M2 + 1)) + (10*(M1 + M2 + 2))
            MXND = MAX(MXTEST, MXND)
            MXNPER = MAX(MXNPER, NPER + 4)
         END IF

C  FLAG THE REGION AS BEING PROCESSABLE

         IREGN(L) = -IREGN(L)

C  MARK THE LINES AND POINTS IN THE REGION AS BEING USED

         CALL MKUSED (MAXNL, MP, ML, LISTL, IPOINT, NINT, LINKP, LINKL,
     &      LCON, NL)

C  CHECK ALL THE HOLES IN THE REGION

         CALL CHKHOL (IA, L, MP, ML, MS, MR, MSC, IPOINT, COOR,
     &      IPBOUN, ILINE, LTYPE, NINT, FACTOR, LCON, ILBOUN, ISBOUN,
     &      ISIDE, NLPS, IFLINE, ILLIST, IREGN, NSPR, IFSIDE, ISLIST,
     &      NPPF, IFPB, LISTPB, NLPF, IFLB, LISTLB, NSPF, IFSB, LISTSB,
     &      IFHOLE, NHPR, IHLIST, LINKP, LINKL, LINKS, LINKR, LINKSC,
     &      LINKPB, LINKLB, LINKSB, RSIZE, NPREGN, NPSBC, NPNODE,
     &      MAXNP, MAXNL, MXNPER, KNBC, KSBC, X, Y, NID, LISTL, MARKED,
     &      MXNL, MAXNBC, MAXSBC, AMESUR, XNOLD, YNOLD, NXKOLD, MMPOLD,
     &      LINKEG, LISTEG, BMESUR, MLINK, NPROLD, NPNOLD, NPEOLD, NNXK,
     &      REMESH, REXMIN, REXMAX, REYMIN, REYMAX, IDIVIS, SIZMIN,
     &      EMAX, EMIN, NOROOM, ERRCHK, ERR)

      END IF

  120 CONTINUE
      RETURN

10000 FORMAT (' ** ERROR - DATA PROBLEMS FOR REGION:', I5, ' **')
10010 FORMAT (' ** ERROR - PERIMETER GENERATION ERRORS FOR REGION:'
     &   , I5, ' **')
10020 FORMAT (' ** ERROR - MAPPING BASE GENERATION ERRORS FOR REGION:'
     &   , I5, ' **')
      END
