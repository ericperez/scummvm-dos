=============================
DOS
=============================

This page contains all the information you need to get ScummVM up and running on an x86 PC running on DOS (FreeDOS, MS-DOS, or other compatible "Disk Operating System.")

What you'll need
===================

- An IBM PC compatible computer with a 32-bit, i586+ CPU (such as an Intel Pentium or AMD K6 CPU.) Later games, especially those with high color or 3D graphics, need a much faster processor (such as a Pentium III or AMD K6-3.)
- 64 MB of RAM or more is recommended, but smaller games may run on 32MB. (ScummVM needs about 30 MB to show the Launcher.)
- FreeDOS, MS-DOS, or other compatible "DOS" to run the executable.
- A VGA-capable graphics card. A VESA compatible card is needed for the high-resolution Launcher, and for games that run at more than 320x200 or in high (15/16-bit) color.
- A DOS-compatible mouse with a DOS mouse driver such as CuteMouse.
- Optional: an AdLib-compatible OPL2/3 sound card for FM-synthesized music.
- Optional: a Sound Blaster (including Pro, 16, etc.) or compatible sound card, for digitized sound and music.
- Optional: an MPU-401 compatible MIDI interface, for a Roland MT-32 or General MIDI sound module, for high-quality MIDI Music.
- Optional: a CD-ROM drive with MSCDEX (or compatible driver), for CD audio.
- Optional: a network card with a DOS packet driver, to transfer game files via the local web server.

Installing ScummVM
=====================================

Download the DOS package from the `ScummVM Downloads page <https://www.scummvm.org/downloads/>`_ (coming soon!)

Extract the ``.zip`` archive to the root of a hard disk, for example ``C:\``. Everything in the archive is inside a ``SCUMMVM`` folder, so extracting it at ``C:\`` will create ``C:\SCUMMVM``. The archive can also be extracted on another computer, and the ``SCUMMVM`` folder copied over to the DOS machine.

The ``SCUMMVM`` folder contains:

``SCUMMVM.EXE``
    The 32-bit DOS ScummVM executable program.

``CWSDPMI.EXE``
    The DPMI (DOS Protected Mode Interface) host that ScummVM needs to run. It is loaded automatically when no other DPMI host is running.

``PLUGINS``
    The game engines--one ``.DXE`` file for each engine. ScummVM for DOS only loads the one engine for the game being played into memory, which allows it to run on systems with as little as 32MB of RAM. 

``OPENMPT.DXE``
    Tracker music (MOD, S3M, XM, IT, etc.) player. Loaded only for games that actually use it.

``DATA``
    Engine data files and launcher translations.

``SCUMMREM.ZIP``, ``SCUMMMOD.ZIP``, ``SCUMMCLA.ZIP``
    The Remastered, Modern, and Classic Launcher GUI themes.

``DOC``, ``LICENSES``
    ScummVM's documentation, and the license files for ScummVM and the libraries it uses.

Starting ScummVM
*******************

Change to the ScummVM folder, and run ``SCUMMVM``:

.. code-block:: bat

    C:
    CD \SCUMMVM
    SCUMMVM

ScummVM looks for its plugins, data files, and configuration file in the "current" folder.

``SCUMMVM.EXE`` accepts the :doc:`command line options <../advanced_topics/command_line>` used by ScummVM on other platforms.

Running ScummVM in DOSBox-X
=============================

ScummVM for DOS also runs in DOSBox-X, a DOS emulator. DOSBox-X has its own mouse driver and Sound Blaster emulation, and sets the ``BLASTER`` variable itself, so nothing else needs to be loaded.

The configuration file below has the settings the DOS port is tested with in DOSBox-X. Save it as a text file, for example ``scummvm.conf``, and change the folder in the ``mount`` line to the folder on your computer that holds the ``SCUMMVM`` folder; on Windows, use a path such as ``C:\dosgames``. Then start DOSBox-X with it:

.. code-block:: bash

    dosbox-x -conf scummvm.conf

.. code-block:: ini

    [dosbox]
    # An S3 graphics card with VESA modes, for the high resolution Launcher and high color games
    machine=svga_s3
    # ScummVM needs about 30 MB of memory to show the Launcher
    memsize=64

    [cpu]
    core=normal
    cputype=pentium_mmx
    # On a fast computer, cycles=max makes games run faster
    cycles=fixed 100000

    [sblaster]
    sbtype=sb16
    sbbase=220
    irq=7
    dma=1
    hdma=5

    [autoexec]
    REM The folder on your computer that holds the SCUMMVM folder
    mount c ~/dosgames
    REM For CD audio, mount the game's CD image, with its audio tracks, as drive D:
    REM imgmount d ~/dosgames/game.cue -t iso
    c:
    cd \SCUMMVM
    SCUMMVM
    exit

The ``exit`` line closes DOSBox-X when you quit ScummVM.

.. note::

    Other versions of DOSBox may also work with similar settings, but have not been tested. DOSBox 0.74 cannot give DOS more than 63 MB of memory, so use ``memsize=63`` there.

Transferring game files
=========================

Game files can be copied to the machine directly from the original floppy disks or CD-ROMs, if available. DOS systems generally run on FAT16 or FAT32 file systems so any machine that can copy files onto either of those can be used, including removable disks, SD cards, USB thumb drives, etc. All major operating systems such as Windows, MacOS, and Linux can copy files to FAT32-formatted drives.

If the DOS machine has a network card (with a packet driver installed), ScummVM's local web server is an easy way to upload game files from another computer. See `Network`_ below, and :doc:`../use_scummvm/LAN`.

.. note::

    DOS file names are limited to eight characters, a dot and a three character extension (8.3). Game files with longer names can only be used when a long file name (LFN) driver, such as DOSLFN, is loaded. LFNs are supported automatically on systems running a DOS "Command Prompt" under Windows systems, starting with Windows 95.

See :doc:`../use_scummvm/game_files` for more information about game file requirements.

Controls
=================

ScummVM is controlled with the keyboard and mouse. Load a DOS mouse driver before starting ScummVM.

Controls can be manually configured in the :doc:`Keymaps tab <../settings/keymaps>`. See the :doc:`../use_scummvm/keyboard_shortcuts` page for common default keyboard shortcuts.

Paths
=======

Saved games
*******************

Saved games are kept in the ScummVM folder, for example ``C:\SCUMMVM``. To keep them in a different folder, change the save path in the :doc:`Paths tab <../settings/paths>`.

Configuration file
**************************

``SCUMMVM.INI`` in the ScummVM folder.

Log file
**********

``SCUMMVM.LOG`` in the ScummVM folder. While ScummVM is in a graphics mode, messages are written to the log file instead of the screen, so check the log file first if something doesn't work.

Settings
==========

For more information about Settings, see the Settings section of the documentation. Only platform-specific differences are listed here.

Graphics
**********

ScummVM always runs full screen.

The Launcher uses the largest high color mode the graphics card offers, up to 1024x768 or the monitor's own resolution, if the monitor reports it. If the card has no suitable mode, the Launcher runs at 640x480. The Launcher resolution can be changed with the ``dos_gui_mode`` and ``dos_gui_max`` configuration keys; see `Configuration keys`_ below.

OpenGL is not available on the DOS port. Games with 3D graphics use the built-in software renderer, which needs a fast processor (Pentium III or higher recommended) to run those games at a decent framerate.

Audio
******

Digital sound and speech play through a Sound Blaster (Original/Pro/16/etc.) or compatible card. The card's settings are read from the ``BLASTER`` environment variable, which the card's setup program normally sets in ``C:\AUTOEXEC.BAT``. For example:

.. code-block:: bat

    SET BLASTER=A220 I5 D1 H5 P330 T6

Supported devices:

- DOS FM hardware (AdLib / Sound Blaster): the card's own OPL2 or OPL3 FM chip. This is used by default when a chip is found. Select it, or one of the emulators, as the :ref:`AdLib emulator <opl>`.
- AdLib emulator: the DOSBox and MAME OPL emulators, when the card has no FM chip.
- DOS MPU-401 MIDI: MIDI output through an MPU-401 compatible interface in UART mode, such as a Sound Blaster's MIDI port, to a Roland MT-32 or a General MIDI sound module. Choose **Roland MT-32 [DOS MPU-401 MIDI]** or **General MIDI [DOS MPU-401 MIDI]** as the :ref:`Preferred device <device>`, :ref:`MT-32 device <mt32>` or :ref:`GM device <gm>`. These devices are only listed when ScummVM finds an interface, and are never selected automatically, because an interface with nothing connected to it would play silence. With a real MT-32, tick :ref:`True Roland MT-32 <nativemt32>`.

The MPU-401 port is taken from the ``mpu_port`` configuration key, then from the ``P`` value of the ``BLASTER`` variable, and is 330 if neither is set.

Supported audio file formats:

- MP3
- OGG
- FLAC
- Uncompressed audio

.. tip::

    Compressed digital audio needs additional processing power to decode. On slower machines, games run better with uncompressed audio (or CD Audio), when available.

CD audio
^^^^^^^^^^

ScummVM for DOS can play the music tracks of a game's original CD from the CD-ROM drive, through MSCDEX or a compatible driver. If the game folder contains ripped audio tracks, those are used instead. To choose the drive, set the ``cdrom`` configuration key to ``0`` for the first CD-ROM drive, ``1`` for the second, and so on, or to a drive letter such as ``D:``.

The drive plays the music itself, through the audio cable between the drive and the sound card. If the CD input of a Sound Blaster Pro or Sound Blaster 16 is muted, ScummVM turns it up while CD audio plays, and puts it back when ScummVM quits. If there is still no music, check the audio cable, and the CD volume in the sound card's mixer program.

Network
*********

ScummVM's :doc:`local web server <../use_scummvm/LAN>` works on DOS. It uses the network card's packet driver, which must be loaded before ScummVM starts. For example, for an NE2000 compatible card:

.. code-block:: bat

    NE2000 0x60 10 0x300

Create a ``WATTCP.CFG`` file in the ScummVM folder with the network settings, for example:

.. code-block:: ini

    my_ip = 192.168.1.50
    netmask = 255.255.255.0
    gateway = 192.168.1.1

To get the address from a DHCP server instead, use ``my_ip = dhcp``.

The network starts the first time the server is run from the LAN tab, not when ScummVM starts. If no packet driver is loaded, the LAN tab is not shown.

Configuration keys
********************

There are some additional configuration keys for the DOS port. Set them in the :doc:`configuration file <../advanced_topics/configuration_file>`.

.. csv-table::
    :widths: 20 15 65
    :header-rows: 1

        Key, Type, Description
        dos_gui_mode, string, "The Launcher resolution: ``auto`` (the default), a size such as ``800x600``, or ``320x200`` for the small Launcher."
        dos_gui_max, string, "The largest resolution ``auto`` chooses for the Launcher. The default is ``1024x768``."
        dos_gui_edid, boolean, "If true (the default), ScummVM asks the monitor for its resolution when choosing the Launcher resolution."
        mpu_port, string, "The I/O port of the MPU-401 MIDI interface, in hexadecimal, such as ``330``."

Known Limitations
=================

- "Cloud" functionality is not supported on ScummVM for DOS. The local web server does work though with a network card (running a DOS packet driver.)
- Hpl1, Thimbleweed Park and The Watchmaker are not included because they need OpenGL. Macs2 is not included, because it needs a debugger that cannot run on DOS.
- Cutscenes in the VP8 or VP9 video formats, used by some SLUDGE games, are skipped.
- Keyboard layouts loaded with ``KEYB`` are not used; the keyboard always works as a US keyboard.
- Sound can play up to about a quarter of a second behind the picture. Setting ``SET SDL_AUDIO_DEVICE_SAMPLE_FRAMES=1024`` before starting ScummVM shortens the delay, but makes gaps in the sound more likely on a slower machine. Long loading pauses, such as a change of scene, can briefly interrupt the sound.
- The picture can tear, because the screen is not synchronized with the monitor's refresh.
- The ``fonts-cjk.dat`` file, used to display Chinese, Japanese and Korean text, is not included because of its size.
