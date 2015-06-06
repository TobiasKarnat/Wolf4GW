ASM     = wasm
CXX     = wpp386
LD      = wlink

INCLUDE = $(%WATCOM)\h

.asm.obj: .AUTODEPEND
  $(ASM) -zq -w0 $*.asm

.cpp.obj: .AUTODEPEND
  $(CXX) -i=$(INCLUDE) -bt=dos -5 -fp5 -fpi87 -ohx -j -zp1 -zq -w0 $*.cpp

OBJS = gamepal3.obj id_ca.obj id_in.obj id_pm.obj id_sd.obj id_us_1.obj id_vh.obj id_vl.obj signon.obj wl_act1.obj wl_act2.obj wl_agent.obj wl_debug.obj wl_draw.obj wl_game.obj wl_inter.obj wl_main.obj wl_menu.obj wl_play.obj wl_state.obj wl_text.obj id_us_a.obj

all: WOLF4GW.EXE .SYMBOLIC

WOLF4GW.EXE: $(OBJS)
  $(LD) name WOLF4GW sys dos32a op el FILE {$(OBJS)}

clean:    .SYMBOLIC
CLEANEXT = sys exe obj err lnk sym lst map lib
  @for %a in ($(CLEANEXT)) do -@rm *.%a
