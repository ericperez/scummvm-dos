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

#ifndef BACKENDS_PLATFORM_DOS_SB_H
#define BACKENDS_PLATFORM_DOS_SB_H

#include <stdint.h>

/**
 * Sound Blaster playback through an auto-init DMA ring buffer: 16-bit stereo on
 * an SB16, 8-bit mono on any card with DSP 2.00 or later.
 *
 * The caller reads the play position (getPlayPosition()) and keeps the ring
 * topped up; the card's interrupt, if any, only silences the blocks played.
 * init() checks that the DMA really runs, and tries the next mode if it does not.
 */
class SoundBlaster {
public:
	enum {
		kRingFrames = 8192,  ///< Size of the DMA ring in frames (one frame = one sample per channel)
		kBlocks = 4          ///< The ring is played as this many blocks; the card interrupts after each
	};

	SoundBlaster();
	~SoundBlaster();

	/**
	 * Find the card (BLASTER, else probe 0x220..0x280), set up DMA and start
	 * playing silence.
	 *
	 * @param requestedRate In Hz. getSampleRate() gives the rate set: in 8-bit mode
	 *        at most 22222, in the DSP's steps; on an SB16 up to 44100.
	 * @param bits 0 = best available, 8 = 8-bit mono, 16 = SB16 16-bit stereo only.
	 * @param useIrq Install the card's interrupt handler (see isIrqDriven()).
	 * @return true if a usable card was found and playback started.
	 */
	bool init(unsigned requestedRate = 22222, int bits = 0, bool useIrq = true);

	/** Stop playback, release DMA and DOS memory. Safe to call twice. */
	void shutdown();

	bool isActive() const {
		return _active;
	}

	/** True for 16-bit signed stereo, false for 8-bit unsigned mono. */
	bool isStereo16() const {
		return _bits16;
	}

	/**
	 * True when the card's interrupt is in use: it silences each block once played,
	 * so a busy program leaves silence rather than a loop of old sound.
	 */
	bool isIrqDriven() const {
		return _irqInstalled;
	}

	/** Overwrite the whole ring with silence. */
	void clearRing() {
		fillSilence();
	}

	/** Bytes per frame: 4 (16-bit stereo) or 1 (8-bit mono). */
	unsigned getFrameBytes() const {
		return _bits16 ? 4 : 1;
	}

	/** Real sample rate (in 8-bit mode set by the DSP time constant, so not exactly 22050). */
	unsigned getSampleRate() const {
		return _rate;
	}

	/** Frame offset into the ring the DMA controller is playing right now. */
	unsigned getPlayPosition();

	/** Copy frames into the ring. frameOffset + frames <= kRingFrames. */
	void writeFrames(unsigned frameOffset, const void *data, unsigned frames);

	/** Why init() failed, or which modes it had to give up on (for the log). */
	const char *getError() const {
		return _error;
	}

	/** Information, for logging */
	unsigned getBase() const {
		return _base;
	}
	int getIrq() const {
		return _irq;
	}
	int getDma() const {
		return _dma;
	}
	int getHdma() const {
		return _hdma;
	}
	int getDspMajor() const {
		return _dspMajor;
	}
	int getDspMinor() const {
		return _dspMinor;
	}

private:
	struct DmaPorts {
		uint16_t addr, count, page, mask, mode, clear;
	};

	bool probe(unsigned base);
	bool resetDsp(unsigned base);
	bool dspWrite(uint8_t value);
	int dspRead();
	bool allocateBlock();
	bool placeRing(unsigned ringBytes, unsigned boundary);
	void fillSilence();
	DmaPorts portsFor(int channel) const;
	void programDma(int channel, unsigned ringBytes);
	bool start8(int channel, unsigned requestedRate);
	bool start16(unsigned requestedRate);
	bool dmaRunning();
	void stopPlayback();
	bool installIrq();
	void removeIrq();
	int readSb16Irq();
	void setError(const char *text);

	bool _active;
	bool _bits16;

	unsigned _base;
	int _irq;
	int _dma;     ///< 8-bit DMA channel (0..3) in use
	int _hdma;    ///< 16-bit DMA channel (5..7) from BLASTER, or -1
	int _dspMajor, _dspMinor;
	unsigned _rate;
	unsigned _ringBytes;

	// DMA memory in conventional memory (below 1MB)

	int _dosSelector;         ///< selector of the conventional memory block allocated
	uint32_t _blockPhys;      ///< where the block allocated is
	uint32_t _physAddr;       ///< the ring inside the block, which does not cross a DMA page

	// PIC mask bit we set for the card's IRQ, so it can be put back

	bool _irqMasked;          ///< the card's IRQ is masked by us
	uint8_t _origPicMask;     ///< the PIC mask before that
	uint16_t _irqPort;        ///< PIC mask port of the card's IRQ (0x21 or 0xA1)
	uint8_t _irqBit;
	bool _cascadeTouched;     ///< IRQ 8-15: we also unmasked IRQ 2 on the master PIC
	uint8_t _origMasterMask;
	bool _irqInstalled;

	int _activeChannel;
	char _error[160];
};

#endif
