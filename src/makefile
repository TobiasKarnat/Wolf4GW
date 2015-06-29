CXX     = wpp386
LD      = wlink

INCLUDE = $(%WATCOM)\h
SODDEF  = -DSPEAR -DSODPAL

all: WOLF4GW.EXE SOD4GW.EXE .SYMBOLIC

WOLF4GW.EXE:
  $(CXX) -i=$(INCLUDE) -bt=dos -5 -fp5 -fpi87 -ohx -j -zp1 -zq -w0 ub.cpp
  $(LD) name WOLF4GW sys dos32a op el FILE ub.obj

SOD4GW.EXE:
  $(CXX) -i=$(INCLUDE) $(SODDEF) -bt=dos -5 -fp5 -fpi87 -ohx -j -zp1 -zq -w0 ub.cpp
  $(LD) name SOD4GW sys dos32a op el FILE ub.obj

clean:    .SYMBOLIC
CLEANEXT = sys exe obj err lnk sym lst map lib
  @for %a in ($(CLEANEXT)) do -@rm *.%a
