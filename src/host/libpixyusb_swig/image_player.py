#!/usr/bin/python

##
# @file image_player.py
# @brief This script plays back the images from the SD Card of the Pixy camera via USB.
#
#
# @copyright Copyright 2021 Matternet. All rights reserved.
#

import collections
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
from PIL import Image, ImageDraw, ImageTk


# Constants related to user interface
WINDOW_WIDTH = 322
WINDOW_HEIGHT = 280
PLAYBACK_DELAY = 100

# Constants related to memory layout of images
BYTES_PER_BLOCK = 512
FRAME_WIDTH = 320
FRAME_HEIGHT = 200
FRAME_HEADER_BLOCK_SIZE = 1
FRAME_HEADER_BYTE_SIZE = FRAME_HEADER_BLOCK_SIZE * BYTES_PER_BLOCK
IMAGE_BYTES = FRAME_WIDTH * FRAME_HEIGHT
BLOCKS_PER_FRAME = IMAGE_BYTES / BYTES_PER_BLOCK + FRAME_HEADER_BLOCK_SIZE
BYTES_PER_FRAME = BLOCKS_PER_FRAME * BYTES_PER_BLOCK
FRAMES_PER_SESSION = 6000
SESSION_BLOCK_START = 2
MAX_SESSIONS = 80

CRC_LEN = 1
HEADER_LEN = 12 + CRC_LEN
MAX_BLOBS = 20
BLOB_STRUCT_ITEM_CNT = 5
BLOB_STRUCT_ITEM_SIZE = 2  # uint16_t
BLOB_STRUCT_LEN = BLOB_STRUCT_ITEM_CNT * BLOB_STRUCT_ITEM_SIZE
BLOB_ARRAY_LEN = MAX_BLOBS * BLOB_STRUCT_LEN
FRAME_HEADER_BEFORE_BLOBS_LEN = 18
FRAME_HEADER_LEN = FRAME_HEADER_BEFORE_BLOBS_LEN + BLOB_ARRAY_LEN + CRC_LEN

FrameHeader = collections.namedtuple('FrameHeader', 'session_cnt '
                                                    'frame_cnt '
                                                    'timestamp_us '
                                                    'last_write_time_us '
                                                    'blob_cnt '
                                                    'blobs '
                                                    'crc8')


## This class maintains the session and frame positions and retrieves the image data via USB.
class Player(object):
    def __init__(self, session_cnt):
        self._session_index = session_cnt % MAX_SESSIONS
        self._frame_index = 0
        self._playing = False
        self._image = None
        self._show_blobs = True

        print("Session count is " + str(session_cnt))
        print("Current session index is " + str(self._session_index))

    def playing(self):
        return self._playing

    def play(self):
        self._playing = True

    def pause(self):
        self._playing = False

    @staticmethod
    def parse_image_header(data):
        hdr = FrameHeader
        hdr.session_cnt, hdr.frame_cnt, hdr.timestamp_us, hdr.last_write_time_us, hdr.blob_cnt = struct.unpack_from('<IIIIH', data)
        hdr.blobs = struct.unpack_from('<100H', data[FRAME_HEADER_BEFORE_BLOBS_LEN:])
        hdr.crc8, = struct.unpack_from('<B', data[-CRC_LEN:])

        crc_func = crcmod.predefined.Crc('crc-8')
        crc_func.update(data[:-CRC_LEN])
        calc_crc8 = int(crc_func.hexdigest(), 16)
        if calc_crc8 == hdr.crc8:
            return hdr
        return None

    def get_image(self, session_index=None, frame_index=None):
        if session_index is None:
            session_index = self._session_index
        if frame_index is None:
            frame_index = self._frame_index

        # Calculate the block to read from SD Card
        session_index = session_index % MAX_SESSIONS
        session_block = SESSION_BLOCK_START + (session_index * BLOCKS_PER_FRAME * FRAMES_PER_SESSION)
        block_num = session_block + (frame_index * BLOCKS_PER_FRAME)

        # Grab frame data
        data = pixy.byteArray(BYTES_PER_FRAME)
        pixy.pixy_read_blocks(block_num, BLOCKS_PER_FRAME, data)

        # Parse frame header
        header_data = pixy.cdata(data, FRAME_HEADER_LEN)
        header = self.parse_image_header(header_data)
        if header:
            print(header.session_cnt, header.frame_cnt, header.timestamp_us / 1000.0, header.last_write_time_us / 1000.0, header.blob_cnt)
        else:
            print('Image header corrupted')

        # Convert to numpy matrix
        frame = np.zeros((FRAME_HEIGHT, FRAME_WIDTH), dtype=np.uint8)
        for h in xrange(FRAME_HEIGHT):
            for w in xrange(FRAME_WIDTH):
                frame[h, w] = data[FRAME_HEADER_BYTE_SIZE + (h * FRAME_WIDTH + w)]

        # Draw frame
        image = Image.fromarray(frame, "L")

        # Draw detected blobs if any
        if self._show_blobs and header:
            draw = ImageDraw.Draw(image)
            for i in range(header.blob_cnt):
                offset = i * BLOB_STRUCT_ITEM_CNT
                top_left = (header.blobs[offset + 1], header.blobs[offset + 3])
                bottom_right = (header.blobs[offset + 2], header.blobs[offset + 4])
                draw.rectangle((top_left, bottom_right), outline=255)
                print("  blob {}: {}, {}".format(i+1, top_left, bottom_right))

        return image

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
    def __init__(self, session_cnt):
        tk.Tk.__init__(self)
        self.title("Pixy Image Player")
        self.geometry("{}x{}".format(WINDOW_WIDTH, WINDOW_HEIGHT))
        self._player = Player(session_cnt)
        self._window = Window(self, self._player)
        self.config(menu=self._window.get_menubar())
        self.mainloop()


## Verifies the header block for corruption.
# @param hdr The header byte data
# @return Current session counter on success or -1 otherwise
def verify_header(hdr):
    magic, version, session_cnt, crc8 = struct.unpack_from('<4sIIB', hdr)
    crc_func = crcmod.predefined.Crc('crc-8')
    crc_func.update(hdr[:-CRC_LEN])
    calc_crc8 = int(crc_func.hexdigest(), 16)
    return session_cnt if (calc_crc8 == crc8) else -1


## Read header block from SD Card.
# @param block_num The block number to read header from (usually 0 or 1)
# @return Data array of header block
def read_header(block_num):
    data = pixy.byteArray(BYTES_PER_BLOCK)
    pixy.pixy_read_blocks(block_num, 1, data)
    return pixy.cdata(data, HEADER_LEN)


## Get session count field of the header blocks
# @return The greater session counter of the two header blocks
def get_session_count():
    # Get HeaderA
    header = read_header(0)
    session_cnt_a = verify_header(header)

    # Get HeaderB
    header = read_header(1)
    session_cnt_b = verify_header(header)

    if session_cnt_a < 0 and session_cnt_a < 0:
        print("Both header blocks are invalid. Proceed with caution...")
        return 0

    return max(session_cnt_a, session_cnt_b)


## Main function of application
def main():
    # Initialize Pixy interface
    if pixy.pixy_init() < 0:
        print("Failed to initialize USB interface")
        sys.exit(-1)

    # Stop default program
    pixy.pixy_command("stop")

    # Get session count to calculate the current session index
    session_cnt = get_session_count()

    # Start application
    app = App(session_cnt)

    # Close connection to Pixy
    pixy.pixy_close()


if __name__ == '__main__':
    main()
