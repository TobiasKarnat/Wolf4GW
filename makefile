ASM     = wasm
CXX     = wpp386
LD      = wlink

INCLUDE = $(%WATCOM)\h

.asm.obj: .AUTODEPEND
  $(ASM) -zq -w0 $*.asm

.cpp.obj: .AUTODEPEND
  $(CXX) -i=$(INCLUDE) -bt=dos -5 -fp5 -fpi87 -ohx -j -zp1 -zq -w0 $*.cpp

OBJS = ub.obj id_us_a.obj

all: WOLF4GW.EXE .SYMBOLIC

WOLF4GW.EXE: $(OBJS)
  $(LD) name WOLF4GW sys dos32a op el FILE {$(OBJS)}

clean:    .SYMBOLIC
CLEANEXT = sys exe obj err lnk sym lst map lib
  @for %a in ($(CLEANEXT)) do -@rm *.%a
