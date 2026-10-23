# JTTY's macros, data/jtty.mc, the one file the radio reads for JTTY: this
# sample is for a chat, and the file is swapped for another for DX or a
# contest. Each macro is sent as one message, and the radio goes back to
# receive when it has gone. A CQ that begins "CQ" and the callsign is what WSJT-X
# reports to PSK Reporter, with the grid if it comes third. {SENTRST} is the
# report in the logger's SENT; ! * and # stand for {CALL}, {MYCALL} and
# {EXCH}, so they are not used as themselves.
F1 CQ, CQ {MYCALL} {GRIDSQUARE}
F2 Answer, {CALL} {MYCALL} {GRIDSQUARE}
F3 Report, {CALL} DE {MYCALL} UR {SENTRST} {SENTRST} BK
F4 Thanks, {CALL} DE {MYCALL} TNX FER QSO 73
F5 Agn?, AGN?
F6 Call, {MYCALL}
