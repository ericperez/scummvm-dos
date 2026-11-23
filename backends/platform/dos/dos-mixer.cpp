/* ScummVM - Graphic Adventure Engine
 *
 * ScummVM is the legal property of its developers, whose names
 * are too numerous to list here. Please refer to the COPYRIGHT
 * file distributed with this source distribution.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

// uclock() gives us a real-time clock for the null device
#define FORBIDDEN_SYMBOL_ALLOW_ALL

#include <stdlib.h>
#include <time.h>
#include <string.h>

#include "backends/platform/dos/dos-mixer.h"
#include "common/config-manager.h"
#include "common/textconsole.h"

DOSMixerManager::DOSMixerManager() :
	MixerManager(),
	_hardware(false), _rate(22050), _writePos(0), _lastTick(0),
	_lastPos(0), _lastPollTick(0), _playAbs(0), _writtenAbs(0), _underruns(0), _stuckSince(0) {
	memset(_mixBuf, 0, sizeof(_mixBuf));
	memset(_outBuf, 0x80, sizeof(_outBuf));
}

DOSMixerManager::~DOSMixerManager() {
	if (_mixer)
		_mixer->setReady(false);
	_sb.shutdown();
}

void DOSMixerManager::init() {
	// output_rate=11025 halves the cost of mixing and OPL emulation, at the price of treble
	unsigned wanted = 22050;
	if (ConfMan.hasKey("output_rate")) {
		int rate = ConfMan.getInt("output_rate");
		if (rate >= 4000)
			wanted = (unsigned)rate;
	}

	// sb_bits (or SCUMMVM_DOS_SB): 8 forces 8-bit mono, 16 insists on SB16 16-bit stereo
	int bits = 0;
	if (ConfMan.hasKey("sb_bits"))
		bits = ConfMan.getInt("sb_bits");
	if (const char *forced = getenv("SCUMMVM_DOS_SB"))
		bits = atoi(forced);

	// sb_irq=false (or SCUMMVM_DOS_SBIRQ=0) turns off the card's interrupt
	bool useIrq = true;
	if (ConfMan.hasKey("sb_irq"))
		useIrq = ConfMan.getBool("sb_irq");
	if (const char *irq = getenv("SCUMMVM_DOS_SBIRQ"))
		useIrq = atoi(irq) != 0;

	_hardware = _sb.init(wanted, bits, useIrq);

	if (_hardware) {
		_rate = _sb.getSampleRate();
		warning("DOS audio: Sound Blaster at %x, IRQ %d, DMA %d, HDMA %d, DSP %d.%02d: %u Hz %s, %s",
		        _sb.getBase(), _sb.getIrq(), _sb.getDma(), _sb.getHdma(), _sb.getDspMajor(), _sb.getDspMinor(), _rate,
		        _sb.isStereo16() ? "16-bit stereo" : "8-bit mono",
		        _sb.isIrqDriven() ? "interrupt handler active" : "polled only");
		if (_sb.getError()[0])
			warning("DOS audio: note: %s", _sb.getError());

		// Start filling a little ahead of wherever the card is by now (it
		// has been playing silence since init() started it)
		_lastPos = _sb.getPlayPosition();
		_playAbs = 0;
		_writtenAbs = kResyncMargin;
		_writePos = (_lastPos + kResyncMargin) % SoundBlaster::kRingFrames;
		_lastPollTick = _stuckSince = (uint64)uclock();
	} else {
		warning("DOS audio: Sound Blaster not usable (%s), sound is off", _sb.getError());
		_rate = 22050;
		_lastTick = (uint64)uclock();
	}

	_mixer = new Audio::MixerImpl(_rate, true, 1024);
	assert(_mixer);
	_mixer->setReady(true);
}

void DOSMixerManager::suspendAudio() {
	_audioSuspended = true;
}

int DOSMixerManager::resumeAudio() {
	if (!_audioSuspended)
		return -2;
	_audioSuspended = false;
	return 0;
}

void DOSMixerManager::produce(unsigned frames, uint8 *out) {
	if (_audioSuspended) {
		memset(out, 0x80, frames); // unsigned 8-bit silence
		return;
	}

	_mixer->mixCallback((byte *)_mixBuf, frames * 4); // 16-bit stereo

	// Nothing playing is the common case: the buffer is all zero, so skip
	// the conversion (three instructions a frame instead of about ten).
	const uint32 *words = (const uint32 *)_mixBuf;
	uint32 any = 0;
	for (unsigned i = 0; i < frames; i++)
		any |= words[i];
	if (!any) {
		memset(out, 0x80, frames);
		return;
	}

	for (unsigned i = 0; i < frames; i++) {
		int mono = ((int)_mixBuf[i * 2] + (int)_mixBuf[i * 2 + 1]) >> 1;
		out[i] = (uint8)((mono >> 8) + 128);
	}
}

void DOSMixerManager::produce16(unsigned frames) {
	if (_audioSuspended) {
		memset(_mixBuf, 0, frames * 4);
		return;
	}

	_mixer->mixCallback((byte *)_mixBuf, frames * 4); // 16-bit stereo, as the card wants it
}

void DOSMixerManager::fallBackToNull(const char *why) {
	warning("DOS audio: %s; carrying on without sound", why);
	_sb.shutdown();
	_hardware = false;
	_lastTick = (uint64)uclock();
}

void DOSMixerManager::updateHardware() {
	const unsigned size = SoundBlaster::kRingFrames;

	unsigned pos = _sb.getPlayPosition();
	uint64 now = (uint64)uclock();

	// uclock() can step back a little: never let time run backwards
	if (now < _lastPollTick)
		now = _lastPollTick;
	const uint64 sinceLast = now - _lastPollTick;

	// How far the card has played since the last call, with the clock counting whole rings
	const unsigned moved = (pos + size - _lastPos) % size;
	const uint64 expected = sinceLast * _rate / UCLOCKS_PER_SEC;
	const uint64 laps = expected > moved ? (expected - moved + size / 2) / size : 0;
	_playAbs += moved + laps * size;

	// A card that stops playing would leave speech waiting forever: go on by the clock
	if (moved != 0 || sinceLast > UCLOCKS_PER_SEC / 10) {
		_stuckSince = now;
	} else if (now - _stuckSince > UCLOCKS_PER_SEC * 4 / 10) {
		fallBackToNull("the Sound Blaster's DMA stopped advancing");
		return;
	}
	_lastPos = pos;
	_lastPollTick = now;

	// The card ran past everything written (the program was busy): restart just ahead of it
	if (_writtenAbs < _playAbs) {
		_underruns++;
		if (_underruns <= 5)
			warning("DOS audio: the program fell behind the sound card by about %u ms; resynchronizing",
			        (unsigned)((_playAbs - _writtenAbs) * 1000 / _rate));

		// Without the interrupt handler old sound is still in the ring
		if (!_sb.isIrqDriven())
			_sb.clearRing();

		_writtenAbs = _playAbs + kResyncMargin;
		_writePos = (pos + kResyncMargin) % size;
	}

	unsigned ahead = (unsigned)(_writtenAbs - _playAbs);

	// Refill in decent-sized pieces: each call to the mixer has a fixed cost
	if (ahead + kMinRefill > kTargetAhead)
		return;

	unsigned need = kTargetAhead - ahead;
	while (need) {
		unsigned chunk = need;
		if (chunk > kChunkFrames)
			chunk = kChunkFrames;
		if (chunk > size - _writePos) // do not wrap inside one write
			chunk = size - _writePos;

		if (_sb.isStereo16()) {
			produce16(chunk);
			_sb.writeFrames(_writePos, _mixBuf, chunk);
		} else {
			produce(chunk, _outBuf);
			_sb.writeFrames(_writePos, _outBuf, chunk);
		}

		_writePos = (_writePos + chunk) % size;
		_writtenAbs += chunk;
		need -= chunk;
	}
}

void DOSMixerManager::updateNull() {
	// No card: still run the mixer at the speed real audio would play, so
	// that anything waiting for a sound to finish does not stall or race.
	uint64 now = (uint64)uclock();
	uint64 due = (now - _lastTick) * _rate / UCLOCKS_PER_SEC;
	if (!due)
		return; // keep the time we have not used yet for the next call

	// After a long stall, do not try to catch up on seconds of audio at once
	const uint64 maxCatchUp = _rate / 4;
	if (due > maxCatchUp) {
		due = maxCatchUp;
		_lastTick = now;
	} else {
		_lastTick += due * UCLOCKS_PER_SEC / _rate;
	}

	while (due) {
		unsigned chunk = due > (unsigned)kChunkFrames ? (unsigned)kChunkFrames : (unsigned)due;
		produce(chunk, _outBuf);
		due -= chunk;
	}
}

void DOSMixerManager::update() {
	if (!_mixer)
		return;

	if (_hardware)
		updateHardware();
	else
		updateNull();
}
