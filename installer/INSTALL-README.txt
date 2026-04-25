DAWvid — CLAP Plugin
====================

All files must stay together in the same folder.
The plugin loads its FFmpeg libraries from its own directory at runtime.


FILES IN THIS FOLDER
--------------------
  DAWvid.clap          the CLAP plugin (all DAWs scan for .clap files)
  avcodec-62.dll       FFmpeg video codec
  avformat-62.dll      FFmpeg container format
  avutil-60.dll        FFmpeg utilities
  swresample-6.dll     FFmpeg audio resampler
  swscale-9.dll        FFmpeg image scaler
  DAWvid.control.js    Bitwig Studio controller script (optional, see below)
  install.bat          Windows installer — double-click to run


INSTALLING WITH THE INSTALLER
------------------------------
Double-click install.bat.

The installer copies everything to:
  C:\Program Files\Common Files\CLAP\DAWvid\

This is the standard location all CLAP-compatible DAWs scan automatically.
If your DAW does not find the plugin after installation, add the folder above
to your DAW's plugin scan paths.


MANUAL INSTALLATION
--------------------
If the installer fails or you prefer to install by hand:

1. Create the folder:
     C:\Program Files\Common Files\CLAP\DAWvid\

2. Copy ALL files from this folder into it:
     DAWvid.clap
     avcodec-62.dll
     avformat-62.dll
     avutil-60.dll
     swresample-6.dll
     swscale-9.dll

   Do not scatter the DLLs elsewhere — they must sit next to DAWvid.clap.

3. Restart your DAW and trigger a plugin scan.

You can also install to a different folder as long as that folder is on
your DAW's CLAP scan path. Keep all the files together in one directory.


BITWIG CONTROLLER SCRIPT (OPTIONAL)
-------------------------------------
DAWvid.control.js enables two features in Bitwig Studio:
  - Playhead position updates while stopped (Bitwig does not send these
    to CLAP plugins via the standard API)
  - Bi-directional seek: scrubbing the video in DAWvid moves the Bitwig
    playhead, and moving the Bitwig playhead updates the video position

To install:
1. Copy DAWvid.control.js to:
     %USERPROFILE%\Documents\Bitwig Studio\Controller Scripts\DAWvid\
   (create the DAWvid sub-folder if it does not exist)

2. In Bitwig Studio, open Settings (gear icon, top-right)
      → Controllers → Add controller
      → Find "DAWvid" in the manufacturer column
      → Select "DAWvid Bridge" → click Add

   The entry will appear as "DAWvid - DAWvid Bridge" in your controller list.

The installer handles this automatically if you click "Install Bitwig Script".


UNINSTALLING
------------
Delete the install folder:
  C:\Program Files\Common Files\CLAP\DAWvid\

No registry entries are created. Removing the folder is a complete uninstall.
Also delete the Bitwig controller script folder if you installed it:
  %USERPROFILE%\Documents\Bitwig Studio\Controller Scripts\DAWvid\
