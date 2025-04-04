/**
 * Furnace Tracker - multi-system chiptune tracker
 * Copyright (C) 2021-2025 tildearrow and contributors
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include "tia.h"
#include "../engine.h"
#include <string.h>
#include <math.h>

#define rWrite(a,v) if (!skipRegisterWrites) {tia.write(a,v); regPool[((a)-0x15)&0x0f]=v; if (dumpWrites) {addWrite(a,v);} }

const char* regCheatSheetTIA[]={
  "AUDC0", "15",
  "AUDC1", "16",
  "AUDF0", "17",
  "AUDF1", "18",
  "AUDV0", "19",
  "AUDV1", "1A",
  NULL
};

const char** DivPlatformTIA::getRegisterSheet() {
  return regCheatSheetTIA;
}

void DivPlatformTIA::acquireDirect(blip_buffer_t** bb, size_t len) {
  thread_local int out[2];
  for (int i=0; i<2; i++) {
    oscBuf[i]->begin(len);
  }

  for (size_t h=0; h<len; h++) {
    int advance=len-h;

    if (tia.myCounter<advance) advance=tia.myCounter;
    if (softwarePitch) {
      if (tuneCounter>=228) {
        if (456-tuneCounter<advance) {
          advance=456-tuneCounter;
        }
      } else {
        if (228-tuneCounter<advance) {
          advance=228-tuneCounter;
        }
      }
    }
    if (advance<1) advance=1;

    if (softwarePitch) {
      int i=-1;
      tuneCounter+=advance;
      if (tuneCounter==228) {
        i=0;
      }
      if (tuneCounter>=456) {
        i=1;
        tuneCounter=0;
      }
      if (i>=0) {
        if (chan[i].tuneCtr++>=chan[i].curFreq) {
          int freq=chan[i].freq;
          chan[i].tuneAcc+=chan[i].tuneFreq;
          if (chan[i].tuneAcc>=256) {
            freq++;
            chan[i].tuneAcc-=256;
          }
          chan[i].curFreq=freq;
          chan[i].tuneCtr=0;
          rWrite(0x17+i,freq);
        }
      }
    }
    tia.tick(advance);

    h+=advance-1;

    if (mixingType==2) {
      out[0]=tia.myCurrentSample[0];
      out[1]=tia.myCurrentSample[1];
    } else if (mixingType==1) {
      out[0]=(tia.myCurrentSample[0]+tia.myCurrentSample[1])>>1;
    } else {
      out[0]=tia.myCurrentSample[0];
    }

    if (out[0]!=prevSample[0]) {
      blip_add_delta(bb[0],h,out[0]-prevSample[0]);
      prevSample[0]=out[0];
    }
    if (mixingType==2) {
      blip_add_delta(bb[1],h,out[1]-prevSample[1]);
      prevSample[1]=out[1];
    }
    oscBuf[0]->putSample(h,tia.myChannelOut[0]); 
    oscBuf[1]->putSample(h,tia.myChannelOut[1]);
  }
  
  for (int i=0; i<2; i++) {
    oscBuf[i]->end(len);
  }
}

unsigned char DivPlatformTIA::dealWithFreq(unsigned char shape, int base, int pitch) {
  int bp=base+pitch;
  double mult=0.25*(parent->song.tuning*0.0625)*pow(2.0,double(768+bp)/(256.0*12.0));
  if (mult<0.5) mult=0.5;
  int ret=0;
  switch (shape) {
    case 1: // buzzy
      ret=ceil(31400/(30.6*mult))-1;
      break;
    case 2: // low buzzy
      ret=ceil(31400/(480*mult))-1;
      break;
    case 3: // flangy
      ret=ceil(31400/(60*mult))-1;
      break;
    case 4: case 5: // square
      ret=ceil(31400/(4.05*mult))-1;
      break;
    case 6: case 7: case 8: case 9: case 10: // pure buzzy/reedy/noise
      ret=ceil(31400/(63*mult))-1;
      break;
    case 12: case 13: // low square
      ret=round(31400/(4.0*3*mult))-1;
      break;
    case 14: case 15: // low pure buzzy/reedy
      ret=ceil(31400/(3*63*mult))-1;
      break;
  }

  if (ret<0) ret=0;
  if (ret>31) ret=31;

  return ret;
}

int DivPlatformTIA::dealWithFreqNew(int shape, int bp) {
  double mult=(parent->song.tuning*0.0625)*pow(2.0,double(768+bp)/(256.0*12.0));
  double clock=chipClock/28.5;
  switch (shape) {
    case 1: // buzzy
      mult*=30;
      break;
    case 2: // low buzzy
      mult*=465;
      break;
    case 3: // flangy
      mult*=58.125;
      break;
    case 4: case 5: // square
      mult*=4;
      break;
    case 6: case 7: case 9: case 10: // pure buzzy/reedy
      mult*=62;
      break;
    case 8: // noise
      mult*=63.875;
      break;
    case 12: case 13: // low square
      mult*=12;
      break;
    case 14: case 15: // low pure buzzy/reedy
      mult*=186;
      break;
  }
  if (mult<1) mult=1;
  if (clock<mult) {
    return 0;
  }
  double ret=floor(clock/mult);
  int fract=((clock/mult)-ret)*256;
  return ret*256+fract-256;
}

void DivPlatformTIA::tick(bool sysTick) {
  for (int i=0; i<2; i++) {
    chan[i].std.next();
    if (chan[i].std.vol.had) {
      chan[i].outVol=VOL_SCALE_LINEAR_BROKEN(chan[i].vol&15,MIN(15,chan[i].std.vol.val),15);
      if (chan[i].outVol<0) chan[i].outVol=0;
      if (isMuted[i]) {
        rWrite(0x19+i,0);
      } else {
        rWrite(0x19+i,chan[i].outVol&15);
      }
    }
    chan[i].handleArp();
    if (chan[i].std.wave.had) {
      chan[i].shape=chan[i].std.wave.val&15;
      rWrite(0x15+i,chan[i].shape);
      chan[i].freqChanged=true;
    }
    if (chan[i].std.pitch.had) {
      if (chan[i].std.pitch.mode) {
        chan[i].pitch2+=chan[i].std.pitch.val;
        CLAMP_VAR(chan[i].pitch2,-32768,32767);
      } else {
        chan[i].pitch2=chan[i].std.pitch.val;
      }
      chan[i].freqChanged=true;
    }
    if (chan[i].freqChanged || chan[i].keyOn || chan[i].keyOff) {
      if (chan[i].fixedArp) {
        chan[i].freq=chan[i].baseNoteOverride&31;
        chan[i].tuneFreq=0;
        if (!skipRegisterWrites && dumpWrites) {
          addWrite(0xfffe0000+i,chan[i].freq*256);
        }
      } else if (oldPitch) {
        int bf=chan[i].baseFreq;
        if (!chan[i].fixedArp) {
          bf+=chan[i].arpOff<<8;
        }
        chan[i].freq=dealWithFreq(chan[i].shape,bf,chan[i].pitch+chan[i].pitch2);
        if (chan[i].shape==4 || chan[i].shape==5) {
          if (bf<39*256) {
            rWrite(0x15+i,6);
            chan[i].freq=dealWithFreq(6,bf,chan[i].pitch+chan[i].pitch2);
          } else if (bf<59*256) {
            rWrite(0x15+i,12);
            chan[i].freq=dealWithFreq(12,bf,chan[i].pitch+chan[i].pitch2);
          } else {
            rWrite(0x15+i,chan[i].shape);
          }
        }
        if (chan[i].freq>31) chan[i].freq=31;
        chan[i].tuneFreq=0;
      } else {
        int bf=chan[i].baseFreq+(chan[i].arpOff<<8);
        int shape=chan[i].shape;
        if (shape==4 || shape==5) {
          if (bf<40*256) {
            shape=6;
            rWrite(0x15+i,6);
          } else if (bf<59*256) {
            shape=12;
            rWrite(0x15+i,12);
          } else {
            rWrite(0x15+i,4);
          }
        }
        bf+=chan[i].pitch+chan[i].pitch2;
        int freq=dealWithFreqNew(shape,bf);
        if (freq>=31*256) freq=31*256;
        if (softwarePitch && !skipRegisterWrites && dumpWrites) {
          addWrite(0xfffe0000+i,freq);
        }
        chan[i].freq=freq>>8;
        chan[i].tuneFreq=freq&255;
      }

      if (chan[i].keyOff) {
        rWrite(0x19+i,0);
      }
      if (!softwarePitch) {
        if (chan[i].tuneFreq>=128) chan[i].freq++;
        rWrite(0x17+i,chan[i].freq);
        if (!skipRegisterWrites && dumpWrites) {
          addWrite(0xfffe0000+i,chan[i].freq<<8);
        }
      }
      if (chan[i].keyOn) chan[i].keyOn=false;
      if (chan[i].keyOff) chan[i].keyOff=false;
      chan[i].freqChanged=false;
    }
  }
}

int DivPlatformTIA::dispatch(DivCommand c) {
  switch (c.cmd) {
    case DIV_CMD_NOTE_ON: {
      DivInstrument* ins=parent->getIns(chan[c.chan].ins,DIV_INS_TIA);
      if (c.value!=DIV_NOTE_NULL) {
        chan[c.chan].baseFreq=c.value<<8;
        chan[c.chan].freqChanged=true;
        chan[c.chan].note=c.value;
      }
      chan[c.chan].active=true;
      chan[c.chan].keyOn=true;
      rWrite(0x15+c.chan,chan[c.chan].shape);
      chan[c.chan].macroInit(ins);
      if (!parent->song.brokenOutVol && !chan[c.chan].std.vol.will) {
        chan[c.chan].outVol=chan[c.chan].vol;
      }
      if (chan[c.chan].insChanged) {
        if (!chan[c.chan].std.wave.will) {
          chan[c.chan].shape=4;
          rWrite(0x15+c.chan,chan[c.chan].shape);
        }
        chan[c.chan].insChanged=false;
      }
      if (isMuted[c.chan]) {
        rWrite(0x19+c.chan,0);
      } else {
        rWrite(0x19+c.chan,chan[c.chan].vol&15);
      }
      break;
    }
    case DIV_CMD_NOTE_OFF:
      chan[c.chan].keyOff=true;
      chan[c.chan].active=false;
      chan[c.chan].macroInit(NULL);
      break;
    case DIV_CMD_NOTE_OFF_ENV:
    case DIV_CMD_ENV_RELEASE:
      chan[c.chan].std.release();
      break;
    case DIV_CMD_VOLUME: {
      chan[c.chan].vol=c.value;
      if (!chan[c.chan].std.vol.has) {
        chan[c.chan].outVol=c.value;
      }
      if (isMuted[c.chan] || !chan[c.chan].active) {
        rWrite(0x19+c.chan,0);
      } else {
        rWrite(0x19+c.chan,chan[c.chan].outVol&15);
      }
      break;
    }
    case DIV_CMD_GET_VOLUME: {
      return chan[c.chan].vol;
      break;
    }
    case DIV_CMD_INSTRUMENT:
      if (chan[c.chan].ins!=c.value || c.value2==1) {
        chan[c.chan].insChanged=true;
      }
      chan[c.chan].ins=c.value;
      break;
    case DIV_CMD_PITCH: {
      chan[c.chan].pitch=c.value;
      chan[c.chan].freqChanged=true;
      break;
    }
    case DIV_CMD_NOTE_PORTA: {
      int destFreq=c.value2<<8;
      bool return2=false;
      if (destFreq>chan[c.chan].baseFreq) {
        chan[c.chan].baseFreq+=c.value;
        if (chan[c.chan].baseFreq>=destFreq) {
          chan[c.chan].baseFreq=destFreq;
          return2=true;
        }
      } else {
        chan[c.chan].baseFreq-=c.value;
        if (chan[c.chan].baseFreq<=destFreq) {
          chan[c.chan].baseFreq=destFreq;
          return2=true;
        }
      }
      chan[c.chan].freqChanged=true;
      if (return2) {
        chan[c.chan].inPorta=false;
        return 2;
      }
      break;
    }
    case DIV_CMD_LEGATO: {
      chan[c.chan].baseFreq=c.value<<8;
      chan[c.chan].freqChanged=true;
      break;
    }
    case DIV_CMD_WAVE:
      chan[c.chan].shape=c.value&15;
      rWrite(0x15+c.chan,chan[c.chan].shape);
      chan[c.chan].freqChanged=true;
      break;
    case DIV_CMD_MACRO_OFF:
      chan[c.chan].std.mask(c.value,true);
      break;
    case DIV_CMD_MACRO_ON:
      chan[c.chan].std.mask(c.value,false);
      break;
    case DIV_CMD_MACRO_RESTART:
      chan[c.chan].std.restart(c.value);
      break;
    case DIV_CMD_GET_VOLMAX:
      return 15;
      break;
    case DIV_CMD_PRE_PORTA:
      if (chan[c.chan].active && c.value2) {
        if (parent->song.resetMacroOnPorta) chan[c.chan].macroInit(parent->getIns(chan[c.chan].ins,DIV_INS_TIA));
      }
      chan[c.chan].inPorta=c.value;
      break;
    case DIV_CMD_PRE_NOTE:
      break;
    case DIV_CMD_EXTERNAL:
      if (!skipRegisterWrites && dumpWrites) {
        addWrite(0xfffe0002,c.value);
      }
      break;
    default:
      //printf("WARNING: unimplemented command %d\n",c.cmd);
      break;
  }
  return 1;
}

void DivPlatformTIA::muteChannel(int ch, bool mute) {
  isMuted[ch]=mute;
  if (isMuted[ch]) {
    rWrite(0x19+ch,0);
  } else {
    if (chan[ch].active) rWrite(0x19+ch,chan[ch].outVol&15);
  }
}

void DivPlatformTIA::forceIns() {
  for (int i=0; i<2; i++) {
    chan[i].insChanged=true;
    if (chan[i].active) {
      chan[i].freqChanged=true;
      if (!isMuted[i]) rWrite(0x19+i,chan[i].outVol&15);
    }
  }
}

void* DivPlatformTIA::getChanState(int ch) {
  return &chan[ch];
}

DivMacroInt* DivPlatformTIA::getChanMacroInt(int ch) {
  return &chan[ch].std;
}

DivDispatchOscBuffer* DivPlatformTIA::getOscBuffer(int ch) {
  return oscBuf[ch];
}

unsigned char* DivPlatformTIA::getRegisterPool() {
  return regPool;
}

int DivPlatformTIA::getRegisterPoolSize() {
  return 6;
}

void DivPlatformTIA::reset() {
  tuneCounter=0;
  prevSample[0]=0;
  prevSample[1]=0;
  tia.reset(mixingType);
  memset(regPool,0,16);
  for (int i=0; i<2; i++) {
    chan[i]=DivPlatformTIA::Channel();
    chan[i].std.setEngine(parent);
    chan[i].vol=0x0f;
  }
}

float DivPlatformTIA::getPostAmp() {
  return 0.5f;
}

int DivPlatformTIA::getOutputCount() {
  return (mixingType==2)?2:1;
}

bool DivPlatformTIA::keyOffAffectsArp(int ch) {
  return true;
}

bool DivPlatformTIA::hasAcquireDirect() {
  return true;
}

bool DivPlatformTIA::getLegacyAlwaysSetVolume() {
  return false;
}

void DivPlatformTIA::notifyInsDeletion(void* ins) {
  for (int i=0; i<2; i++) {
    chan[i].std.notifyInsDeletion((DivInstrument*)ins);
  }
}

void DivPlatformTIA::poke(unsigned int addr, unsigned short val) {
  rWrite(addr,val);
}

void DivPlatformTIA::poke(std::vector<DivRegWrite>& wlist) {
  for (DivRegWrite& i: wlist) rWrite(i.addr,i.val);
}

void DivPlatformTIA::setFlags(const DivConfig& flags) {
  if (flags.getInt("clockSel",0)) {
    chipClock=COLOR_PAL*4.0/5.0;
  } else {
    chipClock=COLOR_NTSC;
  }
  CHECK_CUSTOM_CLOCK;
  rate=chipClock;
  mixingType=flags.getInt("mixingType",0)&3;
  softwarePitch=flags.getBool("softwarePitch",false);
  oldPitch=flags.getBool("oldPitch",false);
  for (int i=0; i<2; i++) {
    oscBuf[i]->setRate(rate);
  }
  tia.reset(mixingType);
}

int DivPlatformTIA::init(DivEngine* p, int channels, int sugRate, const DivConfig& flags) {
  parent=p;
  dumpWrites=false;
  skipRegisterWrites=false;
  mixingType=0;
  for (int i=0; i<2; i++) {
    isMuted[i]=false;
    oscBuf[i]=new DivDispatchOscBuffer;
  }
  setFlags(flags);
  reset();
  return 2;
}

void DivPlatformTIA::quit() {
}
