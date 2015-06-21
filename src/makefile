ASM     = wasm
CXX     = wpp386
LD      = wlink

INCLUDE = $(%WATCOM)\h
SODDEF  = -DSPEAR -DSODPAL

.asm.obj: .AUTODEPEND
  $(ASM) -zq -w0 $*.asm

all: WOLF4GW.EXE SOD4GW.EXE .SYMBOLIC

WOLF4GW.EXE: id_us_a.obj
  $(CXX) -i=$(INCLUDE) -bt=dos -5 -fp5 -fpi87 -ohx -j -zp1 -zq -w0 ub.cpp
  $(LD) name WOLF4GW sys dos32a op el FILE {ub.obj id_us_a.obj}

SOD4GW.EXE: id_us_a.obj
  $(CXX) -i=$(INCLUDE) $(SODDEF) -bt=dos -5 -fp5 -fpi87 -ohx -j -zp1 -zq -w0 ub.cpp
  $(LD) name SOD4GW sys dos32a op el FILE {ub.obj id_us_a.obj}

clean:    .SYMBOLIC
CLEANEXT = sys exe obj err lnk sym lst map lib
  @for %a in ($(CLEANEXT)) do -@rm *.%a
