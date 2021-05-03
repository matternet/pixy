#!/usr/bin/python

##
# @file image_player.py
# @brief This script plays back the images from the SD Card of the Pixy camera via USB.
#
#
# @copyright Copyright 2021 Matternet. All rights reserved.
#

import crcmod
import numpy as np
import pixy
import struct
import sys
import Tkinter as tk
from tkSimpleDialog import askinteger
from tkFileDialog import asksaveasfilename
from tkFileDialog import askdirectory
from ttk import Progressbar
from PIL import Image, ImageTk


# Constants related to user interface
WINDOW_WIDTH = 322
WINDOW_HEIGHT = 280
PLAYBACK_DELAY = 100

# Constants related to memory layout of images
BYTES_PER_BLOCK = 512
FRAME_WIDTH = 320
FRAME_HEIGHT = 200
FRAME_BYTES = FRAME_WIDTH * FRAME_HEIGHT
FRAMES_PER_SESSION = 15000
BLOCKS_PER_FRAME = FRAME_BYTES / BYTES_PER_BLOCK
SESSION_BLOCK_START = 2
MAX_SESSIONS = 32
TIMESTAMP_LEN = 6
HEADER_LEN = 14


## This class maintains the session and frame positions and retrieves the image data via USB.
class Player(object):
    def __init__(self, boot_cnt):
        self._session_index = boot_cnt % MAX_SESSIONS
        self._frame_index = 0
        self._playing = False
        self._image = None

        print("Boot count is " + str(boot_cnt))
        print("Current session index is " + str(self._session_index))

    def playing(self):
        return self._playing

    def play(self):
        self._playing = True

    def pause(self):
        self._playing = False

    def get_image(self, session_index=None, frame_index=None):
        if session_index is None:
            session_index = self._session_index
        if frame_index is None:
            frame_index = self._frame_index

        # Calculate the block to read from SD Card
        session_index = session_index % MAX_SESSIONS
        session_block = SESSION_BLOCK_START + (session_index * BLOCKS_PER_FRAME * FRAMES_PER_SESSION)
        block_num = session_block + (frame_index * BLOCKS_PER_FRAME)

        # Grab image over USB interface
        data = pixy.byteArray(FRAME_BYTES)
        pixy.pixy_read_blocks(block_num, BLOCKS_PER_FRAME, data)

        # Extract the timestamp from the image data and make the pixels black
        timestamp = pixy.cdata(data, TIMESTAMP_LEN)
        boot_cnt, time_ms = struct.unpack_from('>HI', timestamp)
        for i in xrange(TIMESTAMP_LEN):
            data[i] = 0

        print(boot_cnt, time_ms)

        # Convert to numpy matrix
        frame = np.zeros((FRAME_HEIGHT, FRAME_WIDTH), dtype=np.uint8)
        for h in xrange(FRAME_HEIGHT):
            for w in xrange(FRAME_WIDTH):
                frame[h, w] = data[h * FRAME_WIDTH + w]

        return Image.fromarray(frame, "L")

    def get_session_index(self):
        return self._session_index

    def get_frame_index(self):
        return self._frame_index

    def set_frame(self, num):
        if num < 0:
            num = FRAMES_PER_SESSION - 1
        elif num >= FRAMES_PER_SESSION:
            num = 0
        self._frame_index = num

    def set_session(self, num):
        if num < 0:
            num = MAX_SESSIONS - 1
        elif num >= MAX_SESSIONS:
            num = 0
        self._session_index = num

    def increment_frame(self, step):
        self.set_frame(self._frame_index + step)

    def increment_session(self, step):
        self.set_session(self._session_index + step)


## This class builds the user interface and interacts with the Player class to play back images.
class Window(tk.Frame):

    ## Constructor: Builds user interface
    def __init__(self, parent, player):
        tk.Frame.__init__(self, parent)

        # Variables for playback
        self._image = None
        self._player = player

        # Variables for showing current frame information
        self._statusvar = tk.StringVar()
        self._progressvar = tk.IntVar()

        # Build menu
        self._menubar = tk.Menu(self)
        filemenu = tk.Menu(self._menubar, tearoff=0)
        ctrlmenu = tk.Menu(self._menubar, tearoff=0)
        self._menubar.add_cascade(label='File', menu=filemenu)
        self._menubar.add_cascade(label='Controls', menu=ctrlmenu)
        filemenu.add_command(label="Save Frame", command=self.menu_save_frame_clicked)
        filemenu.add_command(label="Save Session", command=self.menu_save_session_clicked)
        ctrlmenu.add_command(label="Goto Session", command=self.menu_ctrl_goto_session_clicked)
        ctrlmenu.add_command(label="Goto Frame", command=self.menu_ctrl_goto_frame_clicked)

        # Build user controls
        self._canvas = tk.Label(self, bg="white")
        self._btn_play = tk.Button(self, text='Play', fg="white", bg="green", activeforeground="white",
                                   activebackground="green", command=self.btn_play_clicked)
        btn_step_frame_up = tk.Button(self, text=">", command=self.btn_step_frame_up_clicked)
        btn_step_frame_dn = tk.Button(self, text="<", command=self.btn_step_frame_dn_clicked)
        btn_step_session_up = tk.Button(self, text=">>", command=self.btn_step_session_up_clicked)
        btn_step_session_dn = tk.Button(self, text="<<", command=self.btn_step_session_dn_clicked)
        lbl_status_bar = tk.Label(self, textvariable=self._statusvar, bd=1, relief=tk.SUNKEN, anchor=tk.W)
        lbl_progress_bar = Progressbar(self, variable=self._progressvar, orient=tk.HORIZONTAL, length=100, mode='determinate')
        lbl_progress_bar.bind("<Button>", self.status_bar_mouse_click)

        # Grid layout
        self._canvas.grid(row=0, column=0, columnspan=5)
        btn_step_session_dn.grid(row=1, column=0)
        btn_step_frame_dn.grid(row=1, column=1)
        self._btn_play.grid(row=1, column=2)
        btn_step_frame_up.grid(row=1, column=3)
        btn_step_session_up.grid(row=1, column=4)
        lbl_progress_bar.grid(row=2, column=0, columnspan=5, pady=5, sticky=tk.NSEW)
        lbl_status_bar.grid(row=3, column=0, columnspan=5, pady=0, sticky=tk.NSEW)
        self.pack()

        # Show the first frame
        self.show_current_frame()

    def get_menubar(self):
        return self._menubar

    def show_current_frame(self):
        progress = int(self._player.get_frame_index() * 100.0 / FRAMES_PER_SESSION)
        self._progressvar.set(progress)
        self._statusvar.set("Session {}, Frame {} of {}".format(self._player.get_session_index()+1, self._player.get_frame_index()+1, FRAMES_PER_SESSION))

        image = self._player.get_image()
        self._image = ImageTk.PhotoImage(image)
        self._canvas.configure(image=self._image)

    def play_next_frame(self):
        self.show_current_frame()
        if self._player.playing():
            self._player.increment_frame(1)
            self.after(PLAYBACK_DELAY, self.play_next_frame)

    def btn_play_clicked(self):
        if self._player.playing():
            self._player.pause()
            self._btn_play.configure(text='Play', bg="green", activebackground="green")
        else:
            self._player.play()
            self._btn_play.configure(text='Pause', bg="red", activebackground="red")
            self.play_next_frame()

    def btn_step_frame_up_clicked(self):
        self._player.increment_frame(1)
        self.show_current_frame()

    def btn_step_frame_dn_clicked(self):
        self._player.increment_frame(-1)
        self.show_current_frame()

    def btn_step_session_up_clicked(self):
        self._player.increment_session(1)
        self._player.set_frame(0)
        self.show_current_frame()

    def btn_step_session_dn_clicked(self):
        self._player.increment_session(-1)
        self._player.set_frame(0)
        self.show_current_frame()

    def status_bar_mouse_click(self, event):
        percent = min(float(event.x) / WINDOW_WIDTH, 1.0)
        index = int(percent * FRAMES_PER_SESSION)
        self._player.set_frame(index)
        self.show_current_frame()

    def menu_save_frame_clicked(self):
        image = self._player.get_image()
        filetypes = [('Bitmap', '*.bmp')]
        filepath = asksaveasfilename(filetypes=filetypes, defaultextension=".bmp")
        image.save(filepath)

    def menu_save_session_clicked(self):
        frame_start = askinteger("Frame Selection", "Enter starting frame number", minvalue=1, maxvalue=FRAMES_PER_SESSION)
        if frame_start is None:
            return
        frame_end = askinteger("Frame Selection", "Enter ending frame number", minvalue=frame_start, maxvalue=FRAMES_PER_SESSION)
        if frame_end is None:
            return

        directory = askdirectory()
        session_index = self._player.get_session_index()
        for frame_index in xrange(frame_start - 1, frame_end):
            image = self._player.get_image(session_index, frame_index)
            image.save("{}/pixy_{}_{}.bmp".format(directory, session_index+1, frame_index+1))

    def menu_ctrl_goto_session_clicked(self):
        session = askinteger("Goto Session", "Enter session number", minvalue=1, maxvalue=MAX_SESSIONS)
        if session is None:
            return
        self._player.set_session(session - 1)
        self._player.set_frame(0)
        self.show_current_frame()

    def menu_ctrl_goto_frame_clicked(self):
        frame = askinteger("Goto Session", "Enter session number", minvalue=1, maxvalue=FRAMES_PER_SESSION)
        if frame is None:
            return
        self._player.set_frame(frame - 1)
        self.show_current_frame()


## This class is the main window for the application.
class App(tk.Tk):
    def __init__(self, boot_cnt):
        tk.Tk.__init__(self)
        self.title("Pixy Image Player")
        self.geometry("{}x{}".format(WINDOW_WIDTH, WINDOW_HEIGHT))
        self._player = Player(boot_cnt)
        self._window = Window(self, self._player)
        self.config(menu=self._window.get_menubar())
        self.mainloop()


## Verifies the header block for corruption.
# @param hdr The header byte data
# @return Current boot counter on success or -1 otherwise
def verify_header(hdr):
    magic, version, boot_cnt, crc16 = struct.unpack_from('<4sIIH', hdr)
    crc_func = crcmod.predefined.Crc('crc-ccitt-false')
    crc_func.update(hdr[:HEADER_LEN-2])
    calc_crc16 = int(crc_func.hexdigest(), 16)
    return boot_cnt if (calc_crc16 == crc16) else -1


## Read header block from SD Card.
# @param block_num The block number to read header from (usually 0 or 1)
# @return Data array of header block
def read_header(block_num):
    data = pixy.byteArray(BYTES_PER_BLOCK)
    pixy.pixy_read_blocks(block_num, 1, data)
    return pixy.cdata(data, HEADER_LEN)


## Get boot count field of the header blocks
# @return The greater boot counter of the two header blocks
def get_boot_count():
    # Get HeaderA
    header = read_header(0)
    boot_cnt_a = verify_header(header)

    # Get HeaderB
    header = read_header(1)
    boot_cnt_b = verify_header(header)

    if boot_cnt_a < 0 and boot_cnt_a < 0:
        print("Both header blocks are invalid. Proceed with caution...")
        return 0

    return max(boot_cnt_a, boot_cnt_b)


## Main function of application
def main():
    # Initialize Pixy interface
    if pixy.pixy_init() < 0:
        print("Failed to initialize USB interface")
        sys.exit(-1)

    # Stop default program
    pixy.pixy_command("stop")

    # Get boot count to calculate the current session index
    boot_cnt = get_boot_count()

    # Start application
    app = App(boot_cnt)

    # Close connection to Pixy
    pixy.pixy_close()


if __name__ == '__main__':
    main()
