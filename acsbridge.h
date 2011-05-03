/*********************************************************************
File: acsbridge.h
Description: a layer above the acsint device driver /dev/acsint.
Acsint carries kernel events such as keystrokes and tty output
back into user space to support accessibility adapters.
You can use the character device directly if you wish,
but that's kinda like reading a block device without
a filesystem on top of it.  You could do it, but you're probably better off
using at least some of the routines in this accessibility bridge.
You can configure the keyboard, watch for events,
maintain a text buffer for each console,
manipulate the reading cursor, watch for index markers
from the synthesizer, manage a repronunciation dictionary, and much more.

There are many routines here; some operate at a low level and some
operate at a higher level.
You should read through the entire api before you start building
your application.
This file is divided into sections of related functions as follows.

Section 1: opening the acsint device.
Section 2: sounds.
Section 3: the reading buffer.
Section 4: capturing keystrokes.
Section 5: key redirection.
Section 6: passing a string to the console as tty input.
Section 7: associate a macro or speech function with a modified key.
Section 8: repronunciations.
Section 9: foreground console.
Section 10: cursor motion.
Section 11: get a chunk of text to read.
Section 12: synthesizer communications.
Section 13: synthesizer speed, volume, pitch, etc.
Section 14: messages from other processes.
*********************************************************************/

#ifndef ACSBRIDGE_H
#define ACSBRIDGE_H

#include "acsint.h"


/*********************************************************************
Section 1: opening the acsint device.
You can't do anything until you open /dev/acsint.
This is the device driver that provides you with
keystroke events, screen memory, and a log of tty output.
Event handlers will be described later.
For now, here are the functions to open and close the device
and check for error conditions.

Although this layer implements a form of encapsulation,
there is one thing we can't hide -
the file descriptor for the open device driver.
Why?
Because any adapter will need to read from multiple devices at once,
and it should do so in a blocking manner.
At a minimum we need to watch for kernel events from the acsint system
and index markers and other feedback from the synthesizer.
Perhaps other stuff too.
Any time you have to read from more than one device simultaneously
you need to use the select(2) call,
and that requires the file descriptors for the various devices.
So let's put it in a global variable for easy access.
However, if you are able to use the acs_ss_wait() and acs_ss_events()
functions described in section 12 then you should do so,
rather than reimplementing the select logic.
*********************************************************************/

extern int acs_fd; // file descriptor
extern int acs_debug; // set to 1 for acs debugging
int acs_log(const char *msg, ...);

// Returns the file descriptor, which is also stored in acs_fd.
// Also opens /dev/vcsa, so you need permission for that.
int acs_open(const char *devname);

// Free the AccessBridge, closing the associated device.
int acs_close(void);

/*********************************************************************
Get the errno or error description in case of a communications error.
This is typically a bad read or write to the device driver.
Note that most commands return -1 for an error and 0 otherwise, like Unix.
Then you can use these functions to get the error number and description.
But really, once the device opens, errors are very unlikely.
We're not talking to a disk, or some peripheral.
Just passing data to and from the kernel.
So whether you do all that error checking is up to you.
I probably won't bother.
*********************************************************************/

int acs_errno(void);
const char *acs_errordesc(void);


/*********************************************************************
Section 2: sounds.
Acsint can generate various sounds
using the pc in-built toggle speaker at port 61.
this is accomplished via another module, pcclicks.ko.
You have to install that module, as well as acsint.ko.
If you don't want any pc sounds, ever, use the parameter
insmod pcclicks enabled=0
If you want to be able to create your own sounds,
but you don't want tty output or printk messages to make noises, use
insmod pcclicks fgtty=0 kmsg=0
Remember that you can set these parameters in /etc/modprobe.conf
You can also turn sounds on and off dynamically, as shown below.
*********************************************************************/

// Any and all sounds, on or off.
int acs_sounds(int enabled) ;

// Clicks and chirps from tty output, on or off.
int acs_tty_clicks(int enabled) ;

// Alert tones from kernel warning/error messages, on or off.
int acs_kmsg_tones(int enabled) ;

// Generate a soft click
int acs_click(void);

// Generaate a quick swoop sound, typically used for newline
int acs_cr(void);

/*********************************************************************
Play a sequence of notes.
Each note is indicated by two shorts.
First is frequency, second is duration in hundredths of a second.
So the standard control g bell sound, 1 khz for a tenth of a second, is
static const short bellbeep[] = { 1000, 10, 0};
0 frequency ends the list of notes.
Use -1 for a rest.
Notes are played in the background; this routine returns immediately.
*********************************************************************/

int acs_notes(const short *notelist);

/*********************************************************************
Play a short low tone if a feature is turned off, or a higher,
slightly longer tone if the feature is turned on.
This is basically a convenient wrapper around acs_notes,
so you can hear when a capability is on or off.
Of course you can also send an appropriate string to the synthesizer:
"feature x is now enabled."
It's up to you.
*********************************************************************/

int acs_tone_onoff(int enabled);

// A beep suitable for control G, but slightly higher than standard.
// Sometimes used for error conditions.
int acs_bell(void);

// Two quick high tones, used for a boundary condition.
// Reading past the end of buffer, entering too much data, etc.
int acs_highbeeps(void);

// A quick high beep used by pcclicks to indicate a capital letter.
int acs_highcap(void);

/*********************************************************************
A low buzz, indicating a serious problem.
I use this when there is no communication with the synthesizer.
Obviously I can't talk at that point, so I just buzz to indicate
that the serial connection is not good (hardware synth),
or the pipe or socket connection to the software synth isn't working,
or the synthesizer is not responding properly.
Also useful if the reading cursor rolls off the end of the buffer.
*********************************************************************/

int acs_buzz(void);


/*********************************************************************
Section 3: the reading buffer.
The reading buffer holds the text that you are going to read.
In screen mode this is a copy of screen memory, also known as a screen snap.
In line mode this is a log of recent tty output,
the last 50,000 characters or so.
either way it is guaranteed to be current and up to date
when your keystroke handler is called.
When you hit F2, I bring the reading buffer
up to date (if necessary), and call your keystroke handler with F2,
whereupon you can commence reading or whatever F2 does.

Characters are stored between start and end.
*start and *end are null, and there are no null characters between.
If start == end then the buffer is empty.
This is impossible in screen mode; there are always 25 rows
and 80 columns of something.  Even blank spaces.
But in line mode there may be nothing if the tty has not generated any output
for that console since the device was opened.

The cursor points to the text you are currently reading.
You should advance this cursor as you read along.
Or let me do it for you via index markers.  See section 11.
The text should probably be treated as readonly.

If lots of tty output pushes your cursor off the back of the buffer,
it will be left as null.
Example: cat a large file.
So be sure to check for null at the top of your event handler.
You may, upon this condition,
stop reading, or sound a buzz, or speak a quick overflow message, or whatever.

marks[] is an array of pointers into the tty buffer.
You can set and read these as you wish.
I move these along with the text, just like the cursor.
Thus you can set locations in your buffer and jump back to them as needed.
They will remain in sync with the moving text.
But like the cursor, they can become null if a lot of output
pushes them off the back end of the buffer.
So check for that.
In some cases it is convenient to use the preprocessor:
#define cutLeftMark rb->marks[0]
Set this to rb->cursor to mark the left boundary
of a block of text that you plan to cut&paste.
This mark remains in sync with the text, even if more output is generated.
Move your cursor to the right edge of the block and issue the cut command.

In screen mode these marks are transient,
and go away if you switch consoles, or switch back to line mode.
They also do not move with scrolling text.  Not implemented yet.

When in screen mode, v_cursor points to the visual cursor on screen.
The reading cursor is set to the visual cursor when
you switch to screen mode.

The characters in the buffer are 4 byte unicodes.
They leave the tty as utf8, or iso8859-x on some older systems,
and are converted by linux vt.c into unicode.
That's the way acsint receives them,
and that's the way it stores them,
and that's the way it passes them down to user space.
The acs_getc routine, described in section 10,
converts these unicodes back into latin-1, so that your adapter
can deal with them as bytes, which is what traditional C does best.
A few unicode routines are provided as well,
to move international characters in and out of the bridge layer.
See acs_getc_uc() and acs_getsentence_uc() below.
And you can tap into the buffer yourself if you like.

Note that this doesn't work in screen mode.
A character in screen memory is a single byte, not a unicode.
I map the byte over to a unicode, to keep a uniform interface,
but I'm sure something gets lost in translation.
Perhaps iso8859-1 is represented faithfully,
but I don't know about other charsets.
It may depend on your locale.
More research is needed here.

To be honest, this entire acsint system is biased towards linear adapters
that read from the tty log.
Many things work in line mode,
that are not yet implemented, and may never be implemented, in screen mode.
*********************************************************************/

#define NUMBUFMARKS 30

struct readingBuffer {
	unsigned int area[TTYLOGSIZE2];
	unsigned char *attribs;
	unsigned int *start, *end;
	unsigned int *cursor;
	unsigned int *v_cursor;
	unsigned int *marks[NUMBUFMARKS];
};

/*********************************************************************
Point to the current reading buffer.
This is a global variable you can use anywhere.
I keep it up to date, even if you switch consoles
or toggle between screen and line mode.
I would declare it const, but you have to be able to update rb->cursor.
*********************************************************************/

extern struct readingBuffer *rb;

/*********************************************************************
Within screen mode, attribs is an array holding the attributes of each character on screen.
Underline, inverse, blinking, etc.
The attribute of the character pointed to by s is rb->attribs[s-rb->start];
No, I don't know what any of the bits mean; guess we'll have to look them up in linux documentation.
A normal character is 7.
*********************************************************************/

/*********************************************************************
Postprocess the text in the buffer, before you try to read it.
This is line mode only.
It is controled by a global variable acs_postprocess
Set the bits for the processing that you want.
Usually you will want them all, and that is the default.

Control H erases the previous character
Turn cr lf into lf
Remove control characters other than bell, cr, lf
Remove the ansi escape codes that move the cursor, set attributes, etc.
These are not text, and can be confusing if mixed into the tty log.
*********************************************************************/

#define ACS_PP_CTRL_H 0x1
#define ACS_PP_CRLF 0x2
#define ACS_PP_STRIP_CTRL 0x4
#define ACS_PP_STRIP_ESCB 0x8

extern int acs_postprocess;

// Clear the buffer, line mode only.
void acs_clearbuf(void);

/*********************************************************************
Alert messages from the kernel, via the printk() call,
do not pass through the tty.
They get sent directly to the console.
In line mode you would not be able to read these messages,
and if ever there was a message you want to read, this is it.
So acsint intercepts these messages and adds the text to the tty log.
There is no interface function here, it just happens automatically.
It will also invoke your more-characters handler
if you have one; just like regular tty output.
*********************************************************************/

/*********************************************************************
Switch between linear and screen mode.
Linear is the default at startup.
*********************************************************************/

void acs_screenmode(int enabled);

/*********************************************************************
Notify the adapter when more characters have been posted to the tty
since your last keystroke or refresh command.
(Remember that I bring the buffer up to date with each key command.)
This is a callback function, or handler, that you provide.
Leave it null if you don't need this information.
echo is 0 for output characters, 1 for an echo of a key that you typed,
and 2 for an indirect echo, such as spaces for tab.
*********************************************************************/

typedef void (*more_handler_t)(int echo, unsigned int c);
extern more_handler_t acs_more_h;


/*********************************************************************
Section 4: capturing keystrokes.
Tell acsint that you are interested in capturing a keystroke.
It will give it to you, and not to the console.
Indicate the key (on your keyboard), and the shift state.
Symbolic names for keys and states are given in acsint.h and linux/input.h -
the latter being included by the former.
So if you want to capture control r, to read the screen or whatever, do this.

acs_setkey(KEY_R, ACS_SS_CTRL);

If another speech function is bound to alt r,
then make these two calls, in addition to the one above.

acs_setkey(KEY_R, ACS_SS_LALT);
acs_setkey(KEY_R, ACS_SS_RALT);

This captures control r, and alt r, but not alt control r.
You would use ACS_SS_LALT|ACS_SS_CTRL for that.

ALT is shorthand for left and right alt.
But don't call setkey with ACS_SS_ALT and expect either alt key to work.
You are really specifying a key chord, and you won't see that key
unless the user is holding down both alt keys simultaneously.
For the typical meaning of alt, you have to issue to setkey commands,
as shown above.
Of course you can just capture left alt r or right alt r if you wish.

acs_unsetkey reverses the action of acs_setkey.
That key, with the specified meta keys held down,
is no longer intercepted, and passes through to the console.

When a key is intercepted, your keyboard handler will be called with
keycode, shiftstate, leds.
Leds are the settings for capslock, numlock, scrolllock.
Example K_NUMLOCK if numlock is on.
See the handler below.

If you are capturing a key from the numeric keypad,
you only get it if numlock is off.
Otherwise it is passed to the console and treated as a number.

Note, these are low level key control functions,
and you probably don't want to use them.
I include them for completeness.
You probably want to use acs_line_configure(), described in section 8.
And acs_reset_configure(), also in section 8, calls clearkeys() for you.
*********************************************************************/

int acs_setkey(int key, int shiftstate);
int acs_unsetkey(int key, int shiftstate);
int acs_clearkeys(void); /* clear all keys */

/* Called when the bridge supplies us with a keystroke. */
typedef void (*key_handler_t) (int key, int shiftstate, int leds);
extern key_handler_t acs_key_h;

/*********************************************************************
You don't get any of these keystroke events until you call acs_events().
Events are read from the device driver at that time,
and your handlers are called.
So you want to use select to monitor all your devices,
acs_fd and synthesizer_fd etc,
and if acs_fd is ready to read, call acs_events().
That will bring in your keystroke events.
It's probably best to just remember the last keystroke event,
i.e. the last speech command issued,
and act on that, in case the user has typed ahead of the adapter.
*********************************************************************/

int acs_events(void);

/*********************************************************************
The refresh command brings the text buffer up to date.
You can call it any time, but the usual procedure
is to call it when you are ready to read the next sentence or line
from the buffer, and your more-chars handler has been called.
New text has been generated, and you want it in hand before you
continue with your reading.

Another use is automatic reading.
You are sitting quietly, waiting for something to happen.
You get the more-chars event.
Call refresh() to update the buffer, then read away.
The user automatically hears any new text that is generated.

You do not need to call this on keystrokes;
the buffer is automatically brought up to date.
*********************************************************************/

int acs_refresh(void);


/*********************************************************************
Section 5: key redirection.
Tell the driver to redirect keystrokes in several different ways.

Bypass sends the next key through to the console,
even if it is a key that you would normally capture.
If you have bound ^C to a speech function, but you want to interrupt
a running program, issue the bypass command, and then type ^C.
It will pass through and perform its regular function.
You can also pass ^S ^Q and ^Z, which have particular meanings in Linux.
Basically anything can be sent through, even the letter t,
which would have gone through anyways.

Use the divert command to divert all future keystrokes to the adapter,
and not to the console.
This is typically used for a text search.
If you want to look for the word foo in the buffer,
then you need to type the word foo into your adapter,
not the console.
Divert makes this possible.
Typically divert is turned back off when the adapter receives the return key.
The string is entered, and we're ready to go.

Finally, monitor can be used to look at every key,
those that are captured and those that go on to the console.
You can use this feature to echo keys as they are pressed.
However, most people respond to the echo characters from the tty,
and speak them then.
Thus you know the computer has responded to the key you pressed.
You typed e, and e has appeared on the screen,
and your adapter says "e".
This is managed through the more_h handler described in section 3.
So monitor is a function that you probably won't need.
*********************************************************************/

int acs_bypass(void);
int acs_divert(int enabled);
int acs_monitor(int enabled);

/*********************************************************************
Use the divert function to capture a string.
This is all handled internally for you.
Pass in the char buffer and its length,
and I will populate it with text entered at the keyboard.
The return key becomes null, and ends the string.
The text must consist of letters, digits, and punctuation,
i.e. the keys on the main block.
Other keys are rejected.
This is limited to ascii for now.
Use the property bits to determine whether bad keys simply beep,
or whether they abort the entry of the string.
See the property bits below for various operational options.
ACS_KS_DEFAULT is a good setting.

Escape always aborts the string, like the user saying "oh never mind."

You may provide an echo callback function to monitor each valid character
as it is typed.
This would only be used if you are in an echo mode and you want to continue
echoing characters as they are typed into the string.

acs_keystring() returns 0 if the string is fetched successfully,
or -1 if it is aborted.

While gathering the string, only /dev/acsint is queried.
If the synthesizer tries to communicate with us,
e.g. passing back index markers or "done speaking" or whatever,
it will have to wait until the string is complete.
This is usually not a problem, because reading is stopped or suspended,
or at least it should be,
while you are typing a string into the adapter.
*********************************************************************/

int acs_keystring(char *buf, int buflen, int properties);

// Sound the bell for bad characters like function keys etc
#define ACS_KS_BADBELL 0x1
// Stop the string when a bad character is entered
#define ACS_KS_BADSTOP 0x2
// call highbeeps() if the user enteres too many characters into the string.
// Running off the end, a boundary condition.
#define ACS_KS_BOUNDARYBEEPS 0x4
// Abort the string if too long.
#define ACS_KS_BOUNDARYSTOP 0x8
// Click as each valid character is entered
#define ACS_KS_GOODCLICK 0x10
// Call acs_cr if the user escapes the string, i.e. types escape
#define ACS_KS_ESCCR 0x20
// Back up via the backspace key or control H
#define ACS_KS_BACKUP 0x40

#define ACS_KS_DEFAULT (ACS_KS_BADBELL|ACS_KS_GOODCLICK|ACS_KS_BOUNDARYBEEPS|ACS_KS_ESCCR|ACS_KS_BACKUP)

// Special handler for acs_keystring echo
typedef void (*ks_echo_handler_t)(int c);
extern ks_echo_handler_t acs_ks_echo_h;

/*********************************************************************
Get one character from the keyboard.
There is no checking here, just return the key and the state.
Could be a function key, whatever.
get1char does more checking - returns a letter or digit.
This can be used to set modes that are 0 to 9,
like the voice,  pitch, rate, etc.
Or choose, by letter, one of a dozen binary modes to toggle.
Or try to open the synthesizer on a different port, 0 through 3,
for ttyS0 through ttyS3.
You get the idea.
Note that get1key is the opposite of bypass.
*********************************************************************/

int acs_get1key(int *key_p, int *ss_p);
int acs_get1char(char *p);


/*********************************************************************
Section 6: passing a string to the console as tty input.
an adapter typically generates input for two reasons.

1. A key can be a macro for a commonly used string,
something that you don't want to type over and over again.
for example, alt F7 is configured,
on my system, to generate the / * line that starts the top of this comment box,
while alt F8 generates the bottom.

2. As a form of cut&paste.
Mark the start and end of a string in the buffer, grab it,
then inject it into the input stream of this session or another session.
I typically cut&paste between two virtual consoles.
Cut&paste is just the greatest thing since sliced bread!

This function pushes characters onto the input stream of the current tty.
In theory you could inject up to 64K of text,
but I've only tested up to a few hundred.
Over 256 is not a problem,
so cut&paste a large block of text if you wish.

The text can be utf8 or an 8859 codepage, consistent with your linux console.
The setmacro() function can similarly accept utf8.
Consider the control #7 macro in acstest.cfg.
Run acstest, switch to another console, run
cat >foo
hit control #7, and then return, and then control d (EOF).
foo will contain exactly the same utf8 characters
as the corresponding line in acstest.cfg.
When doing dynamic cut&paste, grab a block of unicode chars
from the buffer, convert to utf8 using iconv,
put ^#7<  in front (or some such key designator),
and call acs_line_configure().
It should work.
*********************************************************************/

int acs_injectstring(const char *s);


/*********************************************************************
Section 7: associate a macro or speech function with a modified key.
Bind a macro to a key or modified key.
When you hit control F2, for instance,
a certain string is sent to the console via acs_injectstring(),
as described above.

We first map the key code and shift state into a composite number.
This is the modified key code, or mkcode.
alt V is different from control V, etc.
This code is then used to set, clear,
or retrieve the macro associated with the key.
It can also be used to set or clear a speech function bound to that key.

There is an ascii converter as follows:
^V  control V (v can be lower or upper case)
@V  alt V
l@V  left alt V, if left and right alt are treated differently
F2  function key 2
^F7  control F7
@F9  alt F9
r@F9  right alt F9
+F3  shift F3
#0  numpad 0
#.  numpad .
#*  numpad *
#/  numpad /
#-  numpad -
#+  numpad +
^#3  control numpad 3
@#5  alt numpad 5
+#8  shift numpad 8
up  up arrow
down  down arrow
left  left arrow
right  right arrow
^right  control right arrow
home  home
+pause shift pause

These are, once again, low levvel functions,
and you probably should use acs_line_configure() instead.
*********************************************************************/

/* Return the modified key code based on key and state.
 * This assumes numlock is off, and/or the led states don't matter.
 * acsint doesn't capture numlock keypad codes in any case.
 * Returns -1 if the conversion cannot be made. */
int acs_build_mkcode(int keycode, int state);

/* Convert ascii to mkcode.  Pass in a pointer if you want to know
 * where we left off - like strtol(). */
int acs_ascii2mkcode(const char *s, char **endptr);

/* Use the modified key code to set and retrieve a macro string.
 * If the macro starts with | it is executed as a system command.
 * This takes the place of esekeyd, and has the advantage of executing
 * different comands based on shift state - which esekeyd couldn't do.
 * If the leading character is not a pipe then it is just a string,
 * and should be passed to the tty input queue. */
void acs_setmacro(int mkcode, const char *s);

// return 0 if no macro present
char *acs_getmacro(int mkcode);
void acs_clearmacro(int mkcode);

/* Use the modified key code to set and retrieve a speech function.
 * The bytes could be anything, as long as they end in null.
 * You know what "read next line" means, and these functions don't care. */
void acs_setspeechcommand(int mkcode, const char *s);

// return 0 if no speech command present
char *acs_getspeechcommand(int mkcode);
void acs_clearspeechcommand(int mkcode);

/*********************************************************************
acs_events() checks for macros and expands them for you,
using injectstring().
In other words, you only need to handle the speech functions.
*********************************************************************/


/*********************************************************************
Section 8: repronunciations.
Store and retrieve pronunciations for the punctuation marks.
Example:   acs_setpunc('}', "right brace");
Common pronunciations are preloaded, though of course you can change them
based on the user's wishes or a config file.
Characters from 160 to 255 are preset using iso8859-1.

The argument is a unicode, though it can be an iso8859-1 unsigned char,
or an ascii char, since each is compatible with the one before.
Only 2-byte unicodes can be set in this manner.
In other words, the punctuation table has size 65536.
Larger unicodes are simply ignored.
*********************************************************************/

void acs_setpunc(unsigned int c, const char *s);
char *acs_getpunc(unsigned int c);
void acs_clearpunc(unsigned int c);

/*********************************************************************
Replace one word with another for improved pronunciation.
Some synthesizers have on board dictionaries to do this,
but I allow for it here.
That way if you switch synthesizers you still have the same corrections.
For ease of implementation I put limits on the length of a word
and the number of words in the replacement dictionary.

Replacement is case insensitive.
I do not, at this point, atempt to preserve the case after replacement.
So if dog goes to cat, then Dog also goes to cat.

If the second word in setword() is null then the first word
is removed from the dictionary.
*********************************************************************/

#define WORDLEN 18
#define NUMDICTWORDS 1000

int acs_setword(const char *word1, const char *word2);
char *acs_replace(const char *word1);

/*********************************************************************
Smartreplace is a replacement function that understands most English suffixes.
If you have, for instance, replaced computer with compeuter,
then this function maps computers to compeuters as well.
If read goes to reed, then reading goes to reeding.
If library goes to lighbrary, then libraries goes to lighbraries.
I have had to use all these replacements in the past,
to avoid compooter, red, and lib rary.
So this is a smarter replacement dictionary.
It is of course English centered;
folks from other countries will need to reimplement this for their locale.

Note that we require the root word to be replaced with one or more words,
not punctuations etc, so that we can meaningfully put the suffix back on.
*********************************************************************/

char *acs_smartreplace(const char *word1);

/*********************************************************************
At this point we have described four configuration functions:

bind a macro to a modified key
bind a speech function to a modified key
set the pronunciation of a punctuation mark
set the pronunciation of a word

The acs_line_configure() function will do all of these for you.
It is designed to process a line from your config file.
Open file; while read line { acs_line_configure(line) } close
Or you can type the line in at the keyboard
and reconfigure the adapter on the fly.

A blank line, or line beginning with # is ignored.
Use ## to set the pronunciation of #

The four functions have the following syntax.

# macro begins with the modified key follow by less than.
# < is suppose to remind you of getting input, as in <filename
+F3 < this is text that should be sent to the console on shift F3
The text can be international; see the comments on injectstring() above.

#  A pipe implies a system command.
#  Hit alt t, then check the time stamp on /tmp/banana
r@t|touch /tmp/banana

# Without the less than sign or pipe it is assumed to be a speech function
# of your design.  I don't really care what the words are here.
f8 read next line

# two words separated by whitespace becomes a dictionary entry
read reed

# set punctuation pronunciation
}  right brace

A leading at sign, as in @x, means either alt key, not both simultaneously.
It issues two calls to acs_setspeechcommand internally.
That is usually what you want.
Use l@x or r@x if you specifically want one alt key or the other.
Similarly, +@x means control alt x, using either alt key.

You can provide a callback syntax checker, which is invoked for your
speech commands.
If the user types, or places in his config file,
^t garbage
your syntax checker will be called with "garbage",
and you can return -1, because that is not a recognized speech function.
-1 is passed back through, and you can report "syntax error on line 17",
which was trying to associate garbage with control t.
If the speech function is valid, you may encode it in any way you wish.
Replace "read next line" with "12", for instance.
I pass you the string as char * rather than const char *,
so you can modify it if you wish.
But make sure it is still a null terminated string,
because I will associate that string with the modified key
as its speech command.
Within your handler,
call acs_getspeechcommand(KEY_T, ACS_SS_CTRL)
and you will get the string "12".
switch on 12 and do what you are suppose to do.

What about the acsint driver itself?
If you use this function to arrange your macros and key bindings,
and only this function, keystrokes will be captured accordingly.
In other words, I call acs_setkey and acs_unsetkey as needed.

To keep things in sync, use line_configure() all the time,
even with cut&paste.
Mark left, mark right, associate with control t,
Build a line that looks like
^T<text between the two marks
and pass this to line_configure().
*********************************************************************/

typedef int (*syntax_handler_t)(char *s);

int acs_line_configure(char *s, syntax_handler_t syn_fn);

/*********************************************************************
When you first open the acsint device,
I call acs_reset_configure() on your behalf.
This clears all key bindings, and establishes common pronunciations
for various punctuation marks
) right parenthesis
and even some of the higher unicodes.
Call this function yourself when you want to reload the config file.
Many daemons respond to a signal to restart or reload;
that would be a good time to reopen / reset the speech synthesizer,
call this function, and reprocess the config file.
This does not reset any handlers you may have assigned.

I use the global variable acs_lang to set up these common pronunciations.
After all, ) is not called right parenthesis in French.
So you'll want to set this variable before you do anything else.
It could be a parameter to your adapter, or it can be derived from
the environment variable $LANG, as edbrowse does.
If it is not set, reset_configure assumes English.
(For now English is all we have,
so this is mostly planning for the future.)
*********************************************************************/

enum ACS_LANG {
LANG_NONE,
LANG_ENGLISH,
};

extern int acs_lang;

void acs_reset_configure();


/*********************************************************************
Section 9: foreground console.
The bridge maintains the foreground console in a global variable,
and calls your handler to notify you of a console switch.
A console switch erases any keystrokes that are pending in the queue.
They are deemed meaningless if you are changing consoles.
The buffer (screen or line mode) is updated to reflect the new console,
and is brought up to date.
*********************************************************************/

extern int acs_fgc;

// Called when the user switches to a new foreground console.
typedef void (*fgc_handler_t)(void);
extern fgc_handler_t acs_fgc_h;


/*********************************************************************
Section 10: cursor motion.
Every adapter has to move the reading cursor to the beginning of the line,
next line, previous line, start of word, end of word, next word, etc.
These are common functions that you shouldn't have to reinvent.

I choose to operate on a temp cursor, which is initially set
to the reading cursor, and then roams around the buffer.
If there is no next line, or previous line,
or if you are searching for foo and foo is not in your buffer,
then you can cancel the operation and your reading cursor is right
where you left it.
But if the cursor operation is successful you can set
the reading cursor to the temp cursor and off you go.
The "other" design, saving a copy of the reading cursor at the outset,
and then going back to it if the operation is not successful,
is really no harder or easier to implement.
It's 6 of one and half dozen of the other.
So I'm taking door number 1, the temp cursor approach.

The return convention here is different.
I return 1 for success and 0 for failure.
If you can move to the next line, for instance, I return 1;
but if there is no next line I return 0.
0 is always returned for an empty buffer.
There is just nothing to do.
*********************************************************************/

// set the temp cursor to the reading cursor
void acs_cursorset(void);

// Update the reading cursor to agree with the temp cursor.
void acs_cursorsync(void);

/* return the character pointed to by the cursor.
 * Could be null if the buffer is empty.
 * This is downshifted from unicode to iso8859-1.
 * Sorry, no other code pages are implemented at this time.
 * If the unicode cannot be downshifted you will get a question mark.
 * See the downshift() routine in acsbridge.c. */
unsigned int acs_downshift(unsigned int unicode);
int acs_getc(void);
/* And here is the raw unicode version. */
unsigned int acs_getc_uc(void);

/* Advance the cursor.
 * Return 0 if it moves off the end of the buffer.
 * At this point you will probably abort, but if not,
 * be sure to move the cursor back, so it is in-buffer again. */
int acs_forward(void);

/* Back up the cursor.
 * Return 0 if it moves off the end of the buffer.
 * At this point you will probably abort, but if not,
 * be sure to move the cursor forward, so it is in-buffer again. */
int acs_back(void);

// Start and end of line.
// Can only fail if the buffer is empty.
// Start line returns the column number.
int acs_startline(void);
int acs_endline(void);

/*********************************************************************
Start and end of word.  But word is more like a token.
don't is a word, even though it contains an apostrophe.
One apostrophe between letters is tolerated.
3g7j6 is a word, a mix of letters and numbers.
----- is a word, five or more repeated punctuation marks.
This is read as dash length 5, see getsentence() below.
---- is 4 separate words.  That's just my convention.

What about the word niño in Spanish?
I use isalpha() from the ctype library to determine what is a letter,
so if you call setlocale(3), this just might work.
I may indeed recognize this as one word.
*********************************************************************/
int acs_startword(void);
int acs_endword(void);

// start and end of buffer
void acs_startbuf(void);
void acs_endbuf(void);

// Skip past left or right spaces.
void acs_lspc(void);
void acs_rspc(void);

// previous or next line, previous or next word.
// Next word skips past spaces using the rspc routine.
int acs_nextline(void);
int acs_prevline(void);
int acs_nextword(void);
int acs_prevword(void);

/*********************************************************************
Search for a string in the buffer.
The search is case inssensitive.
Upper and lower case letters are determined by the current locale,
so could be set to work in Spanish, French, etc.
The second parameter causes the search to run backward or forward.
The third parameter causes the search to begin on the previous or next line.
Return 1 if the string is found,
whereupon the temp cursor points to the start of the string.
*********************************************************************/
int acs_bufsearch(const char *string, int back, int newline);


/*********************************************************************
Section 11: get a chunk of text to read.
Starting at the reading cursor, fetch text from the buffer
and copy it into a destination array that you specify.
This performs an implicit downshift from unicode to iso8859-1.
Certain unicode characters that are equivalent to apostrophe, dash, star, etc,
become those ascii symbols.
Other high unicodes that have been rendered pronounceable,
by the default values in the bridge layer or by your config file,
are translated into the corresponding words.

If you are reading a word at a time (set ACS_GS_ONEWORD),
I will fetch one word as defined above,
or one unicode / punctuation mark.
This could be a space, newline, control character, etc.
But if you are reading continuously I will copy text up to the end
of the destination array, leaving room for the null byte at the end,
or to the end of the tty buffer, which ever comes first.

If you set ACS_GS_STOPLINE I will stop when I encounter a newline character.
Reading line by line can be helpfull when working on software
or other technical material.
Even if you are reading all the way down the page,
you may still want the stopline feature,
so you can keep the cursor on the correct line,
or pause between lines,
or call acs_cr() for each newline and keep these sounds
in sync with the speech.
Stopline might be off if you are reading a story,
and the punctuations tell all,
and the newlines really mean nothing.
In my world that doesn't happen very often, so stopline is usually set.

Set ACS_GS_NLSPACE if newlines are to be treated as spaces.
this is for the aforementioned story, where newlines mean nothing.
This is incompatible with ACS_GS_ONEWORD or ACS_GS_STOPLINE.

Don't use this function to read a single character.
Just grab rb->cursor[0], or call acs_getc(), and go.
This routine has too much overhead for just one character,
and it does some translations that you may or may not want.

Your destination array should be big enough to hold
a reasonable phrase or sentence.
Yes, you could feed your synthesizer one word at a time,
but the speech is chopppy and unnatural.
You - probably - don't - want - to - do - that.
So make your sentence array 200 bytes or so.

Even at that we could truncate a sentence, or even a single word.
After all, the buffer could contain a string of 10,000 letters.
That is technically one word, but you wouldn't feed that to the synthesizer.
A word should be at most 20 characters.
So when you are fetching a word at a time you should set the length
to 20 or so, rather than the 200 or so that makes sense
for grabbing an entire sentence.

Another translation, which you can turn on or off through ACS_GS_REPEAT,
is the compression of a repeated punctuation mark into one token.
-------------------- is encoded as dash length 20,
or minus length 20, etc, according to your setting of acs_setpunc('-').
I've seen thousands of dots in a row, for instance, when a program
uses these to indicate it is working on a task and making progress.
This translation makes it a single token that fits in a sentence.
Again, you can turn this feature on or off through ACS_GS_REPEAT.

I also compress spaces down to a single space,
and remove space from the beginning or the end of the sentence.
You could get an empty string if the buffer is empty,
or consists entirely of spaces and you are reading continuously.

These, along with the pronounceable high unicodes,
are the only translations that take place here.
Other than that you will receive the text as it appears in the buffer.
There is of course much more translation that could be done.
$3,000 becomes 3 thousand dollars
02/03/2011 becomes February third 2 thousand eleven
3.6 becomes 3 point 6
3.4.5.6 becomes 3 dot 4 dot 5 dot 6
And so on.
These changes, that make text more readable,
will be handled in other routines.
(Run jupiter tts to see such translations.)
We shouldn't try to do everything here.
This is just the first step.

The offset array, which you provide, is optional, and can be 0.
If present it must be as long as the sentence array.
If a word or symbol begins at character 7, that is,
7 bytes past the reading cursor, I will set offset[7] = 7.
Nonzero entries in the offset array are essentially index markers,
indicating where the words and symbols begin.
These can be sent to most synthesizers to keep the reading cursor
in sync with the actual speech.
The synthesizer passes these index markers back to the adapter as it talks,
and the adapter moves the reading cursor along.
So when you interrupt speech you are on the word you last heard.
Of course each synthesizer has its own particular
format and protocol for its index markers.
I'm not trying to anticipate that here.
These are just logical markers where the words begin,
and where they are located relative to the reading cursor.

The last offset, corresponding to the null byte in the sentence array,
is the length of the text consumed, or the offset of the next chunk to read once this one is finished.

Unsigned short is sufficient to represent an offset, since the entire
tty buffer cannot be larger than 64K.
Not that you'd ever want to read a sentence that long anyways.
You'd almost think unsigned char is sufficient, a sentence of length 256,
but the sentence may include a thousand dashes, which are compressed down
to a single token, and so the word after those dashes has index 1043.
I'm going with unsigned short just to be safe.
*********************************************************************/

typedef unsigned short ofs_type;

int acs_getsentence(char *dest, int destlen, ofs_type *offsets, int properties);

/* If you want to manage the unicodes yourself */
int acs_getsentence_uc(unsigned int *dest, int destlen,
ofs_type *offsets, int properties);

#define ACS_GS_ONEWORD 0x1
#define ACS_GS_STOPLINE 0x2
#define ACS_GS_REPEAT 0x4
#define ACS_GS_NLSPACE 0x8


/*********************************************************************
Section 12: synthesizer communications.
Most synthesizers communicate with us over a file descriptor,
be it a serial port, socket, or pipe.
If that is the case you can use ss_fd for this descriptor.
Actually we use ss_fd0 for input and ss_fd1 for output.
These will be the same for a serial port or socket,
and different if we are talking to a software synth through a pipe.
With acs_fd and ss_fd in place, the bridge can perform some functions for you,
like reading from the two file descriptors simultaneously, and watching for events.
We've already seen the acsint events, keystrokes, console switch, etc.
The most common synthesizer events are index markers.
If I am able to capture these events
they will be passed back to you through the following handler,
much like the handlers seen above.
*********************************************************************/

extern int ss_fd0, ss_fd1; /* file descriptors */

/* Which index marker has been returned to us, example 2 out of 5 */
typedef void (*imark_handler_t)(int mark, int lastmark);
extern imark_handler_t ss_imark_h;
extern unsigned int *imark_start; /* for internal bookkeeping */

/* External serial synthesizer, typically /dev/ttySn
 * baud must be one of the standard baud rates from 1200 to 115200
 * Sets ss_fd, and returns same. */
int ess_open(const char *devname, int baud);

/* Close the synthesizer connection, no matter what kind. */
void ss_close(void);

/* The adapter can change the serial flow control on the fly.
 * I've had the cts line fail on my unit, or at least flake out on me.
 * ess_open sets hardware flow control. */
int ess_flowcontrol(int hardware);

/* open a software synth over a pipe.
 * This calls execvp, so the first arg must be
 * the name of the program to run.
 * This will often be the same as progname,
 * unless you want to specify progname with an absolute path. */
int pss_openv(const char *progname,  char * const  alist[]);

/* Like the above, but you pass the arguments inline.
 * This is like execl verses execv.
 * Unlike printfv, I don't have a string with percent directives
 * to tell me how many args you are passing, or the type of each arg.
 * So each arg must be a string, and you must end the list with NULL. */
int pss_open(const char *progname, ...);

/* Check the following variable after anything that might write to or read from the pipe.
 * A broken pipe implies the child process has died. */
extern int pss_broken;

/*********************************************************************
Wait for communication from either the acsint kernel module or the synthesizer.
The return is 1 if acs_fd has data,
2 if ss_fd0 has data,
and 4 if the acsint fifo has an incoming message.
(See section 14 for interprocess messages.)
*********************************************************************/

int acs_ss_wait(void);

/*********************************************************************
Read synthesizer events and call the appropriate handlers.
Events are index markers and talking status.
This is very much like acs_events(), except the internal
details are synthesizer specific.
I need to know the synthesizer style, or I can't watch for these events.
The style is not the same as the model.
There may be several models, including internal cards and external
serial units, that use the same protocol.
So we will need a library of styles, to know how these events
are represented as bytes from the synthesizer,
whereupon I can turn them into standard events and hide
some of these differences from the running adapter.
*********************************************************************/

extern int ss_style;

enum SS_STYLE {
// generic, no index markers etc.
// This one should be first, with a value of 0, hence the default.
SS_STYLE_GENERIC,
// doubletalk, double light, tripletalk, etc
SS_STYLE_DOUBLE,
// Dectalk, pc and express
SS_STYLE_DECEXP,
SS_STYLE_DECPC,
// braille n speak
SS_STYLE_BNS,
// Accent
SS_STYLE_ACE,
};

int ss_events(void);

/* process events from the acsint driver, the synthesizer, or the fifo. */
int acs_ss_events(void);

/*********************************************************************
Ask whether the synthesizer is still talking.
If not, then it is ready for more speech.
This is a subtle function, and its implementation may vary with the style.

One thing we can't do is poll ss_fd1 and ask whether writing would block.
Most units have an on-board buffer and will happily accept
the next sentence while it is in the middle of speaking the current sentence.
And if you're going through a unix pipe, it has an internal buffer too,
before you get to the child process.
This is not very helpful when it comes to synchronized speech.

The Doubletalk uses RI (ring indicator) to tell us whether it is
actually speaking, and that is perfect if you have low level
access to the uart, as we did when the adapter was in the kernel.
We do, as shown by getModemStatus(),
and I may take advantage of this some day.

You could time it, and say each word takes so many seconds to speak
at the current speech rate.
I've done this before, and it's butt ugly!
But it's all you have in SS_STYLE_GENERIC.

The last and best solution is index markers.
Attach a marker to each word, and the unit passes that marker back to you
when it is speaking the corresponding word.
I move the reading cursor along as these markers are returned.
Thus the reading cursor is on the words you are hearing.
This is handled for you in acsbridge.c.
In addition these markers tell us whether the unit is still talking.
So whenever index markers are available here is what I'm going to do.
If you send a one time string to the synth, like reading the current character,
or current word, or announcing "louder" as you increase volume,
or any little snippit of text without index markers,
then I'm going to assume it is instantly read,
and the synthesizer is ready for more text.
The theory is that you won't type faster than it can speak these bits of text,
especially if it is speaking fast, which is usually the case.
There is an exception to this rule; if you hold down a key
that reads the next letter or word, and the key repeats,
then you could buzz through your text reading sequential letters or words.
These could collect in the synthesizer's on-board buffer
and it could have 30 seconds of speech,
and we don't even know it.
it could be "still talking",
and yet ss_stillTalking() returns 0.
I'll try to think of a way around this, but meantime
let's just say that small bits of text, without index markers,
are spoken right away.
In contrast, a sentence or phrase or line
should be sent with index markers,
and those markers are used to track the reading cursor
and maintain talking status.
The unit is "still talking" until the last marker is returned.
And that point it is done talking and the adapter can
gather up and transmit the next sentence.

Thus stillTalking() has two purposes:
to tell the adapter when to send the next sentence,
and to tell the adapter that it should interrupt speech on a keystroke.
It doesn't really mean the unit is talking, it means it is talking
*and* speaking a sentence that should be interrupted,
rather than just the last half second of a snippet of text,
which we don't really need to interrupt.

You don't have to wake up every second and call this function;
set up a handler for index markers,
and each time you get an index marker,
ask whether the unit is still talking.
If not, then send out the next sentence.
If I have done everything right,
then everything is event driven, and you don't have to wake up from time to time
and see if there is something to do.
That's my goal.
*********************************************************************/

int ss_stillTalking(void);

/*********************************************************************
Send a character or a string to the synthesizer to be spoken right away.
I will append the cr, to tell the synth to start speking.
But it isn't always a cr.
The dectalk requires control k cr, just to be perverse.
So as always, you need to set the synthesizer style.

The say_char routine looks in the punctuation pronunciation table,
getpunc(), to see if you have specified a pronunciation.
If not, it just sends the character on to the synthesizer.

This is basically a write(2) call.
It will block if the synth is not ready, but it should be ready.
Even if it is still speaking, it probably has a substantial type ahead buffer.
Most of them do.
Thus you cannot use low level flow control to synchronize speech.
That won't work.
You should be watching for status bits or index markers etc.
So let's say you are doing that, and you believe it is ready to speak
the next item - you can send it out here.
*********************************************************************/

int ss_say_char(char c);
int ss_say_string(const char *s);

/*********************************************************************
Send a string to the synth, but include an index marker for each
nonzero entry in offsets[].
The offset array was built by acs_getsentence().
You may choose to manipulate this array, as you restructure
the sentence for pronunciation purposes,
but each nonzero entry should still mark the start of a word or entity,
and offsets[29] should still be the location in the buffer,
relative to the reading cursor, of the word that begins at s[29].
Note that offsets[29] need not equal 29.
It probably did at the start, but that could change
as you process the sentence.
Consider the example

On 02/02/2003 I graduated.
offsets[17] = 17, placing an index marker at the word graduated.
You rewrite the sentence as
On february second 2 thousand 3 I graduated.
Now offsets[35] = 17.
If you play by these rules, I will keep the reading cursor
on the word last spoken.
I do this by monitoring index markers coming back from the synthesizer.
So you probably don't have to set imark_h (handler) at all.

The actual markers sent to the synth start with firstmark
and increment from there.
Markers cannot go beyond 99.
It's probably safe to set this to 0.

Style must be set properly
so that I know how to send and watch for index markers.
*********************************************************************/

int ss_say_string_imarks(const char *s, const ofs_type *offsets, int firstmark);

/*********************************************************************
Stop speech immediately.
Writes an interrupt byte, which depends on the synth style, to ss_fd1.
Clear away any internal index markers; we're not watching for them any more,
because they aren't coming back to us.
*********************************************************************/

void ss_shutup(void);


/*********************************************************************
Section 13: synthesizer speed, volume, pitch, etc.
Set the volume, pitch, speed, and voice of the speech synthesizer.
The argument is a number from 0 to 9.
In the adapter you might have ^f7 set volume,
and the user can follow that up with a digit, and there you are.

Increment and decrement functions are also provided.
So +f1 could be "softer" while +f2 is "louder".
The new value is stored in ss_curvolume;
So if volume was 6 and you call incvolume(),
ss_curvolume will be 7.
These routines return 0 for success,
-1 if you are moving out of range,
or -2 if the synthesizer does not support changes in volume, pitch, etc.
You may want to issue different error sounds or messages based on -1 or -2.

If the synthesizer does not offer the requested voice then setvoice
returns -2;

This layer hides the differences between speech synthesizers.
They all have their magic codes for changing volume, pitch, etc.
*********************************************************************/

extern int ss_curvolume;
int ss_setvolume(int level);
int ss_incvolume(void);
int ss_decvolume(void);

extern int ss_curpitch;
int ss_setpitch(int level);
int ss_incpitch(void);
int ss_decpitch(void);

extern int ss_curspeed;
int ss_setspeed(int level);
int ss_incspeed(void);
int ss_decspeed(void);

extern int ss_curvoice;
int ss_setvoice(int voice);

/*********************************************************************
When you first open a synthesizer it has certain default values
for speed, volume, pitch, etc.
These, of course, depend on the synthesizer,
and are not known to you, or otherwise predictable, unless you read the manual.
So I set them for you here.
Make sure style is set,
open the synthesizer,
and then call this function.
Check curvolume curspeed curpitch and curvoice for the resulting values.
*********************************************************************/

void ss_startvalues(void);


/*********************************************************************
Section 14: messages from other processes.
Other processes can send messages to your adapter through a fifo.
Each message must end with newline.
The message is then passed back to you through a handler.
the message is allocated; free it when you are done.
You can use this to send text or configuration commands
directly to the synthesizer.
Or you can reconfigure your adapter from another process.
It's up to you.
*********************************************************************/

int acs_startfifo(const char *pathname);
void acs_stopfifo(void);

typedef void (*fifo_handler_t)(char *message);
extern fifo_handler_t acs_fifo_h;


#endif
