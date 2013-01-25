This directory contains PNGs built from a couple of open source fonts: the
sans serif Liberation Mono Regular and the serif Luxi Mono Regular, in 12-,
14, and 24-point versions. All of these font files have anti-aliasing (aka
font smoothing) applied. The directory also includes the original rEFInd
font, which is a 12-point un-smoothed Times-like font.

If you want to create your own fonts, you can do so. If you're using Linux,
the mkfont.sh script will convert an installed MONOSPACE font into a
suitable format. You can use it like this:

./mkfont.sh Liberation-Mono-Italic 14 -1 liberation-mono-italic-14.png

The result is a PNG file, liberation-mono-italic-14.png, that you can copy
to your rEFInd directory and load with the "font" token in refind.conf, as
in:

font liberation-mono-italic-14.png

The mkfont.sh script takes four arguments:

- The font name. Type "convert -list font | less" to obtain a list of
  fonts available on your computer. Note, however, that rEFInd requires
  MONOSPACED (fixed-width) fonts, and most of the fonts installed on most
  computers are variable-width.

- The font size in points.

- A y offset. Many fonts require an adjustment up (negative values), or
  occasionally down (positive values) to fit in the PNG image area. You'll
  have to use trial and error to get this to work.

- The output filename.

I recommend checking the PNG file in a graphics program like eog before
using it. Note that the font files should have an alpha layer, which many
graphics program display as a gray-and-white checkered background.

If you're not using Linux, or if you want to use some other method of
generating fonts, you can do so. The font files must be in PNG format (the
BMP format doesn't support an alpha layer, which is required for proper
transparency). They must contain glyphs for the 95 characters between ASCII
32 (space) and ASCII 126 (tilde, ~), inclusive, plus a 96th glyph that
rEFInd displays for out-of-range characters. To work properly, the
characters must be evenly spaced and the PNG image area must be a multiple
of 96 pixels wide, with divisions at appropriate points. In theory, you
should be able to take a screen shot of a program displaying the relevant
characters and then crop it to suit your needs. In practice, this is likely
to be tedious.

