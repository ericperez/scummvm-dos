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

// Audio CDs through MSCDEX (or a driver with the same calls, as in DOSBox):
//   INT 2Fh AX=1500h  number of CD-ROM drives and the first one's letter
//   INT 2Fh AX=150Bh  is this letter a CD-ROM drive
//   INT 2Fh AX=150Ch  MSCDEX version
//   INT 2Fh AX=1510h  device driver request (play, stop, IOCTL: table of contents, volume)
// The byte layouts and arithmetic are in dos-cd.h. The drive plays into the sound card's CD input
// through its own audio cable, not through ScummVM's mixer.

#define FORBIDDEN_SYMBOL_ALLOW_ALL
#include "common/scummsys.h"

#if defined(USE_DOS_DRIVER)

#include <dos.h>
#include <dpmi.h>
#include <go32.h>
#include <pc.h>
#include <stdlib.h>
#include <string.h>
#include <sys/movedata.h>

#include "common/archive.h"
#include "common/config-manager.h"
#include "common/fs.h"
#include "common/path.h"
#include "common/system.h"
#include "common/textconsole.h"
#include "common/util.h"

#include "backends/audiocd/default/default-audiocd.h"
#include "backends/platform/dos/dos-cd.h"
#include "backends/platform/dos/dos-cdaudio.h"
#include "engines/engine.h"

namespace {

// Where the control block of an IOCTL request sits in the DOS transfer buffer, after the request
const unsigned long kBlockOffset = 64;

// MSCDEX: how many CD-ROM drives there are, and the number of the letter of the first (A is 0)
bool mscdexDrives(int &count, int &first) {
	__dpmi_regs r;
	memset(&r, 0, sizeof(r));
	r.x.ax = 0x1500;
	r.x.bx = 0;
	if (__dpmi_int(0x2f, &r) != 0)
		return false;
	count = r.x.bx;
	first = r.x.cx;
	return count > 0 && count <= 26 && first >= 0 && first < 26;
}

// Does MSCDEX serve this drive letter? Gives its version (major * 100 + minor) if it does.
bool mscdexServes(int letter, int &version) {
	__dpmi_regs r;
	memset(&r, 0, sizeof(r));
	r.x.ax = 0x150B;
	r.x.cx = letter;
	if (__dpmi_int(0x2f, &r) != 0 || r.x.bx != 0xADAD || r.x.ax == 0)
		return false;

	memset(&r, 0, sizeof(r));
	r.x.ax = 0x150C;
	r.x.bx = 0;
	version = 0;
	if (__dpmi_int(0x2f, &r) == 0)
		version = (r.h.bh * 100) + r.h.bl;
	return true;
}

// Send a request to the device driver of a drive, with the control block of an IOCTL request
bool mscdexRequest(int letter, uint8 *request, int length, uint8 *block, int blockLength, uint16 &status) {
	const unsigned long base = __tb;
	if (block)
		dosmemput(block, blockLength, base + kBlockOffset);
	dosmemput(request, length, base);

	__dpmi_regs r;
	memset(&r, 0, sizeof(r));
	r.x.ax = 0x1510;
	r.x.cx = letter;
	r.x.es = base >> 4;
	r.x.bx = base & 15;
	if (__dpmi_int(0x2f, &r) != 0)
		return false;

	// MSCDEX refused it before the driver saw it: carry set, the reason in AX
	if (r.x.flags & 1) {
		status = (uint16)(kDosCdStatusError | (r.x.ax & 0xFF));
		return false;
	}

	uint8 answer[2];
	dosmemget(base + 3, 2, answer);
	status = READ_LE_UINT16(answer);
	if (block)
		dosmemget(base + kBlockOffset, blockLength, block);
	return !dosCdStatusFailed(status);
}

bool mscdexIoctl(int letter, bool output, uint8 *block, int length, uint16 *statusOut = nullptr) {
	const unsigned long address = __tb + kBlockOffset;
	uint8 request[kDosCdIoctlRequestSize];
	const int size = dosCdBuildIoctl(request, output, (uint16)(address >> 4), (uint16)(address & 15), length);

	uint16 status = 0;
	const bool ok = mscdexRequest(letter, request, size, block, length, status);
	if (statusOut)
		*statusOut = status;
	return ok;
}

// The table of contents of the disc in a drive
bool mscdexReadToc(int letter, DosCdToc &result) {
	DosCdToc toc;
	uint8 block[16];

	dosCdBuildDiscInfo(block);
	if (!mscdexIoctl(letter, false, block, kDosCdDiscInfoSize) || !dosCdParseDiscInfo(block, toc))
		return false;

	for (int track = toc.first; track <= toc.last; track++) {
		dosCdBuildTrackInfo(block, track);
		if (!mscdexIoctl(letter, false, block, kDosCdTrackInfoSize) || !dosCdParseTrackInfo(block, track, toc))
			return false;
	}

	if (!toc.valid())
		return false;
	result = toc;
	return true;
}

// A Sound Blaster mixer register: an index port and a data port, with no interrupt in between
uint8 mixerRead(unsigned base, uint8 reg) {
	const int enabled = disable();
	outportb(base + 4, reg);
	const uint8 value = inportb(base + 5);
	if (enabled)
		enable();
	return value;
}

void mixerWrite(unsigned base, uint8 reg, uint8 value) {
	const int enabled = disable();
	outportb(base + 4, reg);
	outportb(base + 5, value);
	if (enabled)
		enable();
}

class DosAudioCDManager : public DefaultAudioCDManager {
public:
	DosAudioCDManager();
	~DosAudioCDManager() override;

	bool open() override;
	void close() override;
	bool play(int track, int numLoops, int startFrame, int duration, bool onlyEmulate = false,
	          Audio::Mixer::SoundType soundType = Audio::Mixer::kMusicSoundType) override;
	void stop() override;
	bool isPlaying() const override;
	void setVolume(byte volume) override;
	void setBalance(int8 balance) override;
	void update() override;
	bool isAudioDiscInDrive() override;

	/** Stop the drive and put the sound card's CD input back, as the program leaves */
	void shutdown();

	bool hasDrive() const {
		return _drive >= 0;
	}
	const DosCdToc &toc() const {
		return _toc;
	}
	bool readToc();

protected:
	bool openCD(int drive) override;
	bool openCD(const Common::Path &drive) override;

private:
	bool configuredDrive(int &letter);
	bool driveLetter(int drive, int &letter);
	bool driveLetter(const Common::Path &drive, int &letter);
	bool openDrive(int letter);
	bool noDrive();
	bool ioctl(bool output, uint8 *block, int length, uint16 *status = nullptr) const;
	bool driveBusy() const;
	bool startPlay();
	void stopDrive();
	void applyVolume();
	void raiseCdInput();
	void saveCdInput(uint8 reg, uint8 value);
	void restoreCdInput();

	int _drive;           ///< number of the drive letter MSCDEX plays from (A is 0), -1: none
	DosCdToc _toc;
	DosCdRange _range;    ///< what is playing
	DosCdLoop _loop;
	bool _toldNoDrive;
	bool _toldPlaying;

	// The sound card's CD input, if it was silent and raised: the registers as they were
	bool _cdInputChecked;
	unsigned _cdMixerBase;
	int _cdSavedCount;
	uint8 _cdSavedReg[3];
	uint8 _cdSavedValue[3];
};

DosAudioCDManager *g_dosCd = nullptr;

DosAudioCDManager::DosAudioCDManager() : _drive(-1), _toldNoDrive(false), _toldPlaying(false),
	_cdInputChecked(false), _cdMixerBase(0), _cdSavedCount(0) {
	_range.start = _range.frames = 0;
	g_dosCd = this;
}

DosAudioCDManager::~DosAudioCDManager() {
	close();
	g_dosCd = nullptr;
}

bool DosAudioCDManager::open() {
	close();

	if (openRealCD())
		return true;

	// No drive: the tracks come from files (the default manager's emulation)
	return DefaultAudioCDManager::open();
}

void DosAudioCDManager::close() {
	DefaultAudioCDManager::close(); // stops what plays
	restoreCdInput();
	_cdInputChecked = false;
	_drive = -1;
	_toc = DosCdToc();
	_loop.cancel();
}

void DosAudioCDManager::shutdown() {
	if (_drive >= 0 && (_loop.loops != 0 || driveBusy()))
		stopDrive();
	_loop.cancel();
	restoreCdInput();
}

// The one note in the log that there is no CD drive to play from
bool DosAudioCDManager::noDrive() {
	if (!_toldNoDrive) {
		_toldNoDrive = true;
		warning("DOS: no CD-ROM drive found (MSCDEX is not loaded, or there is no drive): CD audio plays from track files only");
	}
	return false;
}

// The letter of CD-ROM drive number drive (0 is the first)
bool DosAudioCDManager::driveLetter(int drive, int &letter) {
	int count, first;
	if (!mscdexDrives(count, first))
		return noDrive();

	if (drive < 0 || drive >= count) {
		warning("DOS: CD drive %d asked for, but there are %d CD-ROM drive(s): CD audio plays from track files only",
		        drive, count);
		return false;
	}
	letter = first + drive;
	return true;
}

bool DosAudioCDManager::driveLetter(const Common::Path &drive, int &letter) {
	int count, first;
	if (!mscdexDrives(count, first))
		return noDrive();

	letter = dosCdDriveLetter(drive.toString('/').c_str());
	if (letter < first || letter >= first + count) {
		warning("DOS: '%s' is not a CD-ROM drive: CD audio plays from track files only", drive.toString('/').c_str());
		return false;
	}
	return true;
}

// The drive the "cdrom" setting names, read as openRealCD() reads it
bool DosAudioCDManager::configuredDrive(int &letter) {
	const Common::String cdrom = ConfMan.get("cdrom");
	char *end;
	const long drive = strtol(cdrom.c_str(), &end, 0);
	if (end == cdrom.c_str())
		return driveLetter(Common::Path::fromConfig(cdrom), letter);
	return drive >= 0 && driveLetter((int)drive, letter);
}

bool DosAudioCDManager::openCD(int drive) {
	int letter;
	return driveLetter(drive, letter) && openDrive(letter);
}

bool DosAudioCDManager::openCD(const Common::Path &drive) {
	int letter;
	return driveLetter(drive, letter) && openDrive(letter);
}

bool DosAudioCDManager::openDrive(int letter) {
	int version;
	if (!mscdexServes(letter, version))
		return noDrive();

	_drive = letter;

	// What the driver says it can do, for the log
	uint8 block[kDosCdDeviceStatusSize];
	uint32 device = 0;
	dosCdBuildDeviceStatus(block);
	const bool known = ioctl(false, block, kDosCdDeviceStatusSize) && dosCdParseDeviceStatus(block, device);
	if (known && !(device & kDosCdDevicePlaysAudio))
		warning("DOS: the driver of CD drive %c: says it cannot play audio tracks", 'A' + letter);
	const Common::String driver = known ? Common::String::format("device status %04X", (unsigned)device) : "no device status";

	if (readToc())
		warning("DOS: CD audio from drive %c: (MSCDEX %d.%02d, %s), music tracks %d to %d", 'A' + letter,
		        version / 100, version % 100, driver.c_str(), _toc.first, _toc.last);
	else
		warning("DOS: CD audio from drive %c: (MSCDEX %d.%02d, %s), no disc to read yet", 'A' + letter,
		        version / 100, version % 100, driver.c_str());
	return true;
}

bool DosAudioCDManager::isAudioDiscInDrive() {
	int letter, version;
	DosCdToc toc;
	return configuredDrive(letter) && mscdexServes(letter, version) && mscdexReadToc(letter, toc) && toc.hasMusic();
}

bool DosAudioCDManager::ioctl(bool output, uint8 *block, int length, uint16 *statusOut) const {
	return mscdexIoctl(_drive, output, block, length, statusOut);
}

bool DosAudioCDManager::readToc() {
	DosCdToc toc;
	if (_drive < 0 || !mscdexReadToc(_drive, toc))
		return false;
	_toc = toc;
	return true;
}

// MSCDEX says a drive is playing audio by the busy bit of the status of any request
bool DosAudioCDManager::driveBusy() const {
	if (_drive < 0)
		return false;
	uint8 block[16];
	uint16 status = 0;
	dosCdBuildAudioStatus(block);
	ioctl(false, block, kDosCdAudioStatusSize, &status);
	return dosCdStatusPlaying(status);
}

bool DosAudioCDManager::startPlay() {
	uint8 request[kDosCdPlayRequestSize];
	const int size = dosCdBuildPlay(request, _range.start, _range.frames);
	uint16 status = 0;
	if (!mscdexRequest(_drive, request, size, nullptr, 0, status)) {
		warning("DOS: the CD drive refused to play (status %04X)", (unsigned)status);
		return false;
	}
	return true;
}

void DosAudioCDManager::stopDrive() {
	uint8 request[kDosCdHeaderSize];
	const int size = dosCdBuildStop(request);
	uint16 status = 0;
	mscdexRequest(_drive, request, size, nullptr, 0, status);
}

void DosAudioCDManager::applyVolume() {
	if (_drive < 0)
		return;
	byte left, right;
	dosCdChannelVolumes(_cd.volume > 255 ? 255 : (byte)_cd.volume, (int8)_cd.balance, left, right);
	uint8 block[kDosCdChannelsSize];
	dosCdBuildChannels(block, left, right);
	ioctl(true, block, kDosCdChannelsSize);
}

void DosAudioCDManager::saveCdInput(uint8 reg, uint8 value) {
	_cdSavedReg[_cdSavedCount] = reg;
	_cdSavedValue[_cdSavedCount] = value;
	_cdSavedCount++;
}

void DosAudioCDManager::restoreCdInput() {
	while (_cdSavedCount > 0) {
		_cdSavedCount--;
		mixerWrite(_cdMixerBase, _cdSavedReg[_cdSavedCount], _cdSavedValue[_cdSavedCount]);
	}
}

// The sound card mixes the drive's music in through its CD input. One left silent (an SB16's is
// until a mixer program sets it) is raised to the voice's level until the CD manager is closed.
void DosAudioCDManager::raiseCdInput() {
	if (_cdInputChecked)
		return;
	_cdInputChecked = true;

	unsigned base;
	const DosCdMixer mixer = dosCdBlasterMixer(getenv("BLASTER"), base);
	_cdMixerBase = base;
	uint8 level;

	if (mixer == kDosCdMixerSb16) {
		const uint8 left = mixerRead(base, kDosCdMixerSb16CdLeft);
		const uint8 right = mixerRead(base, kDosCdMixerSb16CdRight);
		const uint8 switches = mixerRead(base, kDosCdMixerSb16Switches);
		const uint8 voice = MAX(mixerRead(base, kDosCdMixerSb16VoiceLeft), mixerRead(base, kDosCdMixerSb16VoiceRight));
		const bool silent = dosCdSb16CdLevel(left, right, voice, level);
		const uint8 newSwitches = dosCdSb16Switches(switches);
		if (!silent && newSwitches == switches)
			return;

		saveCdInput(kDosCdMixerSb16Switches, switches);
		saveCdInput(kDosCdMixerSb16CdLeft, left);
		saveCdInput(kDosCdMixerSb16CdRight, right);
		if (silent) {
			mixerWrite(base, kDosCdMixerSb16CdLeft, level);
			mixerWrite(base, kDosCdMixerSb16CdRight, level);
		}
		mixerWrite(base, kDosCdMixerSb16Switches, newSwitches);

		// A card without this mixer does not keep what was written
		const bool kept = (!silent || (mixerRead(base, kDosCdMixerSb16CdLeft) & 0xF8) == level) &&
		                  (mixerRead(base, kDosCdMixerSb16Switches) & kDosCdMixerSb16SwitchesCd) != 0;
		if (!kept) {
			restoreCdInput();
			warning("DOS: the sound card at %x has its CD input silent, and no SB16 mixer to raise it with", base);
			return;
		}
		warning("DOS: the sound card's CD input was silent: SB16 mixer at %x, CD volume %d to %d of 31, output switches %02X to %02X, until ScummVM quits",
		        base, left >> 3, (silent ? level : left) >> 3, switches, newSwitches);
	} else if (mixer == kDosCdMixerSbPro) {
		const uint8 cd = mixerRead(base, kDosCdMixerSbProCd);
		if (!dosCdSbProCdLevel(cd, mixerRead(base, kDosCdMixerSbProVoice), level))
			return;

		saveCdInput(kDosCdMixerSbProCd, cd);
		mixerWrite(base, kDosCdMixerSbProCd, level);
		if ((mixerRead(base, kDosCdMixerSbProCd) & 0xEE) != (level & 0xEE)) {
			restoreCdInput();
			warning("DOS: the sound card at %x has its CD input silent, and no SB Pro mixer to raise it with", base);
			return;
		}
		warning("DOS: the sound card's CD input was silent: SB Pro mixer at %x, CD volume %02X to %02X, until ScummVM quits",
		        base, cd, level);
	}
}

bool DosAudioCDManager::play(int track, int numLoops, int startFrame, int duration, bool onlyEmulate,
                             Audio::Mixer::SoundType soundType) {
	// The tracks as files first, as on every other system
	if (DefaultAudioCDManager::play(track, numLoops, startFrame, duration, onlyEmulate, soundType))
		return true;

	if (onlyEmulate || _drive < 0)
		return false;

	if (numLoops == 0 && startFrame == 0)
		return false;

	// The table of contents is read again on every play: the disc may have been changed
	if (!readToc()) {
		warning("DOS: no music CD in the drive (track %d asked for)", track);
		return false;
	}

	// "Fix MI1 CD Audio (hopefully)": the other CD audio managers do the same
	if (duration > 0)
		duration += 5;

	// ScummVM counts the disc's tracks from 0
	const int physical = dosCdPhysicalTrack(_toc, track);
	DosCdRange range;
	if (physical < 0 || !dosCdTrackRange(_toc, physical, startFrame, duration, range)) {
		warning("DOS: track %d (from frame %d) is not music on this disc (tracks 0 to %d; a data track cannot be played)",
		        track, startFrame, _toc.last - _toc.first);
		return false;
	}

	_range = range;
	raiseCdInput();

	// Some drivers do not pass on a volume they take to be set already: it goes through 0 first
	uint8 silence[kDosCdChannelsSize];
	dosCdBuildChannels(silence, 0, 0);
	ioctl(true, silence, kDosCdChannelsSize);
	applyVolume();
	if (!startPlay())
		return false;

	if (!_toldPlaying) {
		_toldPlaying = true;
		warning("DOS: CD drive %c: plays disc track %d; its music goes by the drive's audio cable to the sound card's CD input",
		        'A' + _drive, physical);
	}

	_loop.begin(numLoops, g_system->getMillis(), range.frames);
	return true;
}

void DosAudioCDManager::stop() {
	DefaultAudioCDManager::stop();

	if (_drive >= 0 && (_loop.loops != 0 || driveBusy()))
		stopDrive();
	_loop.cancel();
}

bool DosAudioCDManager::isPlaying() const {
	if (DefaultAudioCDManager::isPlaying())
		return true;

	if (_drive < 0 || _loop.loops == 0)
		return false;

	// Between the end of one play and the start of the next
	if (_loop.loops != 1)
		return true;

	const uint32 now = g_system->getMillis();
	const bool late = (int32)(now - _loop.endTime) >= 0;
	return dosCdLoopPlaying(_loop, now, late && driveBusy());
}

void DosAudioCDManager::setVolume(byte volume) {
	DefaultAudioCDManager::setVolume(volume);
	applyVolume();
}

void DosAudioCDManager::setBalance(int8 balance) {
	DefaultAudioCDManager::setBalance(balance);
	applyVolume();
}

void DosAudioCDManager::update() {
	DefaultAudioCDManager::update();

	if (_drive < 0 || _loop.loops == 0)
		return;

	const uint32 now = g_system->getMillis();
	if ((int32)(now - _loop.endTime) < 0)
		return;

	switch (dosCdLoopStep(_loop, now, driveBusy())) {
	case kDosCdReplay:
		if (startPlay())
			_loop.endTime = now + dosCdFramesToMillis(_range.frames);
		else
			_loop.cancel();
		break;
	case kDosCdDone:
		// A drive that still says it is playing long after the end is told to stop
		if (driveBusy())
			stopDrive();
		break;
	case kDosCdWait:
		break;
	}
}

// ---- The self-test ----

struct Checks {
	int count;
	bool failed;
	Checks() : count(0), failed(false) {}

	void check(bool condition, const char *what) {
		count++;
		if (!condition) {
			failed = true;
			warning("CDTEST: FAILED %s", what);
		}
	}
};

// An engine that runs nothing, to make the calls a game with CD audio makes as it starts
class CdTestEngine : public Engine {
public:
	CdTestEngine() : Engine(g_system) {}

	Common::Error run() override {
		return Common::kNoError;
	}
};

// Let the program run (sound, timers) for a while, calling the CD manager's update as it does
void runFor(uint32 milliseconds) {
	const uint32 begin = g_system->getMillis();
	while (g_system->getMillis() - begin < milliseconds) {
		g_system->delayMillis(20);
		g_system->getAudioCDManager()->update();
	}
}

// Until it stops, or `limit` milliseconds: how long did it play
uint32 timeUntilSilent(uint32 limit) {
	const uint32 begin = g_system->getMillis();
	while (g_system->getAudioCDManager()->isPlaying() && g_system->getMillis() - begin < limit) {
		g_system->delayMillis(20);
		g_system->getAudioCDManager()->update();
	}
	return g_system->getMillis() - begin;
}

} // namespace

AudioCDManager *dosCreateAudioCDManager() {
	return new DosAudioCDManager();
}

void dosCdShutdown() {
	if (g_dosCd)
		g_dosCd->shutdown();
}

void dosCdSelfTest(const char *mode) {
	AudioCDManager *cd = g_system->getAudioCDManager();
	Checks c;

	if (!cd || !g_dosCd) {
		warning("CDTEST: FAILED the DOS CD manager is not there");
		return;
	}

	const bool withDrive = strcmp(mode, "drive") == 0;

	// The track files of the test are in the folder the program runs in
	SearchMan.addDirectory("cdtest", Common::FSNode(Common::Path(".", '/')), 0, 1, false);

	// An SB16's CD input made silent, to see it raised for the drive's music and put back on close
	unsigned base = 0;
	const bool sb16 = withDrive && dosCdBlasterMixer(getenv("BLASTER"), base) == kDosCdMixerSb16;
	uint8 cdLevels[2] = {0, 0};
	if (sb16) {
		cdLevels[0] = mixerRead(base, kDosCdMixerSb16CdLeft);
		cdLevels[1] = mixerRead(base, kDosCdMixerSb16CdRight);
		mixerWrite(base, kDosCdMixerSb16CdLeft, 0);
		mixerWrite(base, kDosCdMixerSb16CdRight, 0);
	} else if (withDrive) {
		warning("CDTEST: FAILED BLASTER (%s) names no SB16, so the CD input is not tested", getenv("BLASTER") ? getenv("BLASTER") : "not set");
		c.failed = true;
	}

	// What SCUMM and Groovie do as a game with CD audio starts, before they open the CD: with the
	// disc in the drive no dialog may stop the program here (the test would never finish)
	c.check(cd->isAudioDiscInDrive() == withDrive, withDrive ? "the audio CD in the drive is not seen" : "an audio CD is seen with no drive");
	if (withDrive) {
		CdTestEngine engine;
		if (!engine.existExtractedCDAudioFiles() && !engine.isDataAndCDAudioReadFromSameCD())
			engine.warnMissingExtractedCDAudio();
	}

	c.check(cd->open(), "open() fails (it should fall back to track files at worst)");

	if (withDrive) {
		c.check(g_dosCd->hasDrive(), "no CD drive found through MSCDEX");
		const DosCdToc &toc = g_dosCd->toc();
		Common::String lengths;
		for (int t = toc.first; t <= toc.last; t++)
			lengths += Common::String::format("%s%u", t == toc.first ? "" : ",", (unsigned)toc.length(t));
		warning("CDTEST: disc tracks %d-%d lengths %s", toc.first, toc.last, lengths.c_str());

		// ScummVM counts the tracks from 0: 0, 1 and 2 are the three tracks of the disc.
		// Track 1 (the disc's second), all of it: plays, then stops when told to
		c.check(cd->play(1, 1, 0, 0, false), "play(1) was refused");
		if (sb16)
			c.check((mixerRead(base, kDosCdMixerSb16CdLeft) >> 3) != 0, "the silent CD input was not raised for the drive's music");
		runFor(500);
		c.check(cd->isPlaying(), "track 1 is not reported playing");

		// Where the drive is (Q channel): near the start of the disc's track 2. A start sector off
		// by the lead-in would be 2 seconds further on, or in track 1.
		int count = 0, first = 0;
		uint8 q[11];
		memset(q, 0, sizeof(q));
		q[0] = 12;
		const bool qRead = mscdexDrives(count, first) && mscdexIoctl(first, false, q, sizeof(q));
		warning("CDTEST: the drive is in disc track %d, %d:%02d.%02d into it", q[2], q[4], q[5], q[6]);
		c.check(qRead && q[2] == 2 && q[4] == 0 && q[5] < 2, "the drive did not start at the disc's track 2");
		cd->stop();
		runFor(200);
		c.check(!cd->isPlaying(), "track 1 is still reported playing after stop()");

		// One second of track 2 (the last), from 1 s in: ends by itself after about a second
		cd->setVolume(200);
		cd->setBalance(-40);
		c.check(cd->play(2, 1, 75, 75, false), "play(2, from 75, 75 frames) was refused");
		const uint32 once = timeUntilSilent(8000);
		warning("CDTEST: one second of track 2 took %u ms", (unsigned)once);
		c.check(once >= 800 && once <= 3500, "one second of track 2 did not take about a second");

		// The same twice over: takes about twice as long
		c.check(cd->play(2, 2, 150, 38, false), "play(2, twice) was refused");
		const uint32 twice = timeUntilSilent(8000);
		warning("CDTEST: half a second of track 2, twice, took %u ms", (unsigned)twice);
		c.check(twice >= 1000 && twice <= 4500, "two plays did not take twice as long as one");

		// Track 0 (the disc's first track): its last second
		c.check(cd->play(0, 1, 225, 75, false), "play(0, last second) was refused");
		const uint32 last = timeUntilSilent(8000);
		warning("CDTEST: the last second of track 0 took %u ms", (unsigned)last);
		c.check(last >= 800 && last <= 2500, "the last second of track 0 did not end with the track");

		// Things that are not there
		c.check(!cd->play(3, 1, 0, 0, false), "track 3 of a three track disc (tracks 0 to 2) was played");
		c.check(!cd->play(7, 1, 0, 0, false), "track 7 of a three track disc was played");
		c.check(!cd->play(-1, 1, 0, 0, false), "track -1 was played");
		c.check(!cd->play(1, 0, 0, 0, false), "a play of no loops from the start was not refused");
		c.check(!cd->play(1, 1, 100000, 0, false), "a start past the end of the track was played");
		c.check(!cd->isPlaying(), "something plays after the refused plays");
	} else {
		c.check(!g_dosCd->hasDrive(), "a CD drive was found");
		c.check(cd->existExtractedCDAudioFiles(2), "track2.wav is not found");
		c.check(cd->play(2, 1, 0, 0, false), "the track file did not play");
		runFor(300);
		c.check(cd->isPlaying(), "the track file is not reported playing");
		cd->stop();
		c.check(!cd->isPlaying(), "the track file is still playing after stop()");
		c.check(!cd->play(5, 1, 0, 0, false), "track 5, which has no file and no disc, was played");
	}

	cd->close();
	if (sb16) {
		const uint8 afterClose = mixerRead(base, kDosCdMixerSb16CdLeft);
		c.check((afterClose >> 3) == 0, "the CD input was not put back on close");
		mixerWrite(base, kDosCdMixerSb16CdLeft, cdLevels[0]);
		mixerWrite(base, kDosCdMixerSb16CdRight, cdLevels[1]);
		warning("CDTEST: SB16 CD input put back to %d of 31 on close", afterClose >> 3);
	}

	if (c.failed)
		warning("CDTEST: FAILED (%d checks)", c.count);
	else
		warning("CDTEST: OK %d checks (%s)", c.count, withDrive ? "with a CD" : "no drive, track files");
}

#endif // USE_DOS_DRIVER
