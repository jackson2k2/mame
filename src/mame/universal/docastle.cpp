// license:BSD-3-Clause
// copyright-holders:Brad Oliver
/***********************************************************************

Mr. Do's Castle

FIRST CPU:
0000-7fff ROM
8000-97ff RAM
9800-99ff Sprites
b000-b3ff Video RAM
b400-b7ff Color RAM

read:
a000-a008 data from second CPU

write:
a000-a008 data for second CPU
a800      Watchdog reset?
e000      Trigger NMI on second CPU

SECOND CPU:
0000-3fff ROM
8000-87ff RAM

read:
a000-a008 data from first CPU
all the following ports can be read both from c00x and from c08x. I don't know
what's the difference.
c001      DSWB
c081      coins per play
c002      DSWA
          bit 6-7 = lives
          bit 5 = upright/cocktail (0 = upright)
          bit 4 = difficulty of EXTRA (1 = easy)
          bit 3 = unused?
          bit 2 = RACK TEST
          bit 0-1 = difficulty
c003      IN0
          bit 4-7 = joystick player 2
          bit 0-3 = joystick player 1
c004      flipscreen
c005      IN1
          bit 7 = START 2
          bit 6 = unused
          bit 5 = jump player 2
          bit 4 = fire player 2
          bit 3 = START 1
          bit 2 = unused
          bit 1 = jump player 1(same effect as fire)
          bit 0 = fire player 1
c085      during the boot sequence, clearing any of bits 0, 1, 3, 4, 5, 7 enters the
          test mode, while clearing bit 2 or 6 seems to lock the machine.
c007      IN2
          bit 7 = unused
          bit 6 = unused
          bit 5 = COIN 2
          bit 4 = COIN 1
          bit 3 = PAUSE
          bit 2 = SERVICE (keep pressed)
          bit 1 = TEST (doesn't work?)
          bit 0 = TILT

write:
a000-a008 data for first CPU
e000      sound port 1
e400      sound port 2
e800      sound port 3
ec00      sound port 4


Mr. Do Wild Ride / Mr. Do Run Run memory map (preliminary)

0000-1fff ROM
2000-37ff RAM
3800-39ff Sprites
4000-9fff ROM
a000-a008 Shared with second CPU
b000-b3ff Video RAM
b400-b7ff Color RAM

write:
a800      Watchdog reset?
b800      Trigger NMI on second CPU (?)

SECOND CPU:
0000-3fff ROM
8000-87ff RAM

read:
e000-e008 data from first CPU
c003      bit 0-3 = joystick
          bit 4-7 = ?
c004      flipscreen
c005      bit 0 = fire
          bit 1 = fire (again?!)
          bit 2 = ?
          bit 3 = START 1
          bit 4-6 = ?
          bit 4 = START 2
c081      coins per play

write:
e000-e008 data for first CPU
a000      sound port 1
a400      sound port 2
a800      sound port 3
ac00      sound port 4

Notes:
------
- All inputs are read through the TMS1025N2LC at D8 (low 4 bits) and F8
  (high 4 bits). Pins 11-16, 18 and 27-34 of both devices are linked to the
  edge connector, though several of these input lines are typically unused.
- idsoccer seems to run on a modified version of this board which allows for
  more sprite tiles; it also has an extra sound board for ADPCM playback.

TODO:
-----
- Third CPU. According to schematics, it's a doorway for the sprite RAM. The
  main cpu actually doesn't have full access to sprite RAM. But instead, 0x9800
  to 0x9fff is a latch to the 3rd cpu. CPU3 reads 0x200 bytes from cpu1, and
  then passes it through (unmodified) presumedly to the sprite chip. It looks
  very much like hardware protection. And it's easy to circumvent in MAME by
  pretending the cpu1 directly writes to sprite RAM.
- docastle schematics say that cpu1(and cpu3) interrupt comes from the 6845
  CURSOR pin. The cursor is configured at scanline 0, and causes the games to
  update the next video frame during active display. What is the culprit here?
  For now, it's simply hooked up to vsync.
- dip switch reading bug. dorunrun and docastle are VERY timing sensitive, and
  dip switch reading will fail if timing is not completely accurate.
- the dorunrun attract mode sequence is also very timing sensitive. The behaviour
  of the "dorunrun2" set, verified on the real board, with all dips in the OFF
  position (easiest difficulty setting), should be, for the first 12 rounds of
  demo play:
  1) Mr do moves right. Kills all monsters and a letter 'E'.
  2) Mr do moves Left. Picks up 'A' letter, but dies.
  3) Mr do moves right. Kills all monsters.
  4) Mr do moves left. Kills 'X' monster.
  5) Mr do moves right.
  6) Mr do moves left. Get 'T' monster.
  7) Mr do moves right.
  8) Mr do moves left. Kills 'E' monster - but already has this.
  9) Mr do moves right.
  10) Mr do moves left. Kills 'R' monster.
  11) Mr do moves right.
  12) Mr do moves. Kills 'A' monster. Shows winning extra Mr do.

  Small changes to the CPU speed or refresh rate alter the demo timing and can cause
  the above sequence (not) to work (e.g. Mr Do might not kill all monsters at once
  on the first round).
  Note that this only works in the dorunrun2 set. The dorunrun set works slightly
  differently, however it hasn't been compared with the real board so it might be
  right.

- bad communication in idsoccer
- adpcm status in idsoccer
- real values for the adpcm interface in idsoccer
- bad adpcm sound in attract mode for idsoccer clones (once you get them to boot up)
- idsoccer clones lock up MAME after writing junk to CRTC. Workaround:
  idsoccera/idsoccert: cpu1 bp 1120 -> pc=1123 -> run
  asoccer: cpu1 bp 1120 -> pc=1135 -> run
  Or simply ROM_FILL(0x247, 1, 0x23 or 0x35 instead of 0x20) on the slave cpu (not
  guaranteed that a permanent ROM hack like this won't break anything).
  This 0x1120 is a return address written to the stack after communicating with
  the slave CPU. Maybe protection. See MT05419.


2008-08
Dip locations verified with manual for docastle, dorunrun and dowild.

	// when video RAM is selected
	// when color RAM is selected
	// when sprite RAM is selected

	// when R6 and R7 are activated

***********************************************************************/

#include "emu.h"

#include "cpu/z80/z80.h"
#include "machine/watchdog.h"
#include "machine/tms1024.h"
#include "sound/msm5205.h"
#include "sound/sn76496.h"
#include "video/mc6845.h"
#include "video/resnet.h"

#include "emupal.h"
#include "screen.h"
#include "speaker.h"
#include "tilemap.h"

#define USE_LATCH false

namespace {

class docastle_state : public driver_device
{
public:
	docastle_state(machine_config const &mconfig, device_type type, char const *tag) :
		driver_device(mconfig, type, tag),
		m_cpu(*this, "cpu%u", 1U),
		m_watchdog(*this, "watchdog"),
		m_inp(*this, "inp%u", 1U),
		m_screen(*this, "screen"),
		m_crtc(*this, "crtc"),
		m_gfxdecode(*this, "gfxdecode"),
		m_palette(*this, "palette"),
		m_sn76489(*this, "sn%u", 1U),
		m_videoram(*this, "videoram"),
		m_colorram(*this, "colorram"),
		m_spriteram(*this, "spriteram")
	{ }

	void dorunrun(machine_config &config);
	void docastle(machine_config &config);

protected:
	// initialization
	virtual void machine_start() override;
	virtual void machine_reset() override;
	virtual void video_start() override;
	virtual void video_reset() override;

	// devices
	required_device_array<z80_device, 3> m_cpu;
	required_device<watchdog_timer_device> m_watchdog;
	required_device_array<tms1025_device, 2> m_inp;
	required_device<screen_device> m_screen;
	required_device<hd6845s_device> m_crtc;
	required_device<gfxdecode_device> m_gfxdecode;
	required_device<palette_device> m_palette;
	required_device_array<sn76489a_device, 4> m_sn76489;

	// memory pointers
	required_shared_ptr<u8> m_videoram;
	required_shared_ptr<u8> m_colorram;
	required_shared_ptr<u8> m_spriteram;

	// video-related
	void set_tile_mask(u16 const mask) { m_tile_mask = mask; }

	tilemap_t *m_tilemap = nullptr;

	u8 cpu1_latch_r(offs_t offset);
	void cpu1_latch_w(offs_t offset, u8 data);
	u8 cpu2_latch_r(offs_t offset);
	void cpu2_latch_w(offs_t offset, u8 data);
	void nmi_trigger_w(u8 data);
	void videoram_w(offs_t offset, u8 data);
	void colorram_w(offs_t offset, u8 data);
	u8 inputs_flipscreen_r(offs_t offset);
	void flipscreen_w(offs_t offset, u8 data);

	TILE_GET_INFO_MEMBER(get_tile_info);
	void palette(palette_device &palette) const;
	u32 screen_update(screen_device &screen, bitmap_rgb32 &bitmap, rectangle const &cliprect);
	void draw_sprites(screen_device &screen, bitmap_rgb32 &bitmap, rectangle const &cliprect);
	void virtual extend_sprite(bool &flipy, u16 &code, u8 &color) {};
	void tint(int state);

	// address maps
	void docastle_map1(address_map &map);
	void dorunrun_map1(address_map &map);
	void docastle_map2(address_map &map);
	void dorunrun_map2(address_map &map);
	void docastle_map3(address_map &map);
	void docastle_io_map(address_map &map);

private:
	// internal state
	bool m_prev_ma6 = false;

#if USE_LATCH
	// u8 m_latch = 0x00U;
#else
	u8 m_buffer0[9]{};
	u8 m_buffer1[9]{};
#endif

	u16 m_tile_mask = 0x00ffU;
};

class idsoccer_state : public docastle_state
{
public:
	idsoccer_state(machine_config const &mconfig, device_type type, char const *tag) :
		docastle_state(mconfig, type, tag),
		m_msm(*this, "msm"),
		m_adpcm(*this, "adpcm")
	{ }

	void idsoccer(machine_config &config);

protected:
	virtual void machine_start() override;
	virtual void machine_reset() override;

private:
	// devices
	required_device<msm5205_device> m_msm;

	// memory pointers
	required_region_ptr<u8> m_adpcm;

	// ADPCM
	u8 adpcm_status_r();
	void adpcm_w(u8 data);
	void adpcm_int(int state);

	// video-related
	void extend_sprite(bool &flipy, u16 &code, u8 &color) override;

	// address maps
	void idsoccer_map1(address_map &map);

	// internal state
	int m_adpcm_pos = 0;
	int m_adpcm_idle = 0;
	int m_adpcm_data = 0;
	int m_adpcm_status = 0;
};

// Video
void docastle_state::palette(palette_device &palette) const
{
	u8 const *const prom = memregion("proms")->base();
	static int constexpr resistances[3] = { 200, 390, 820 };

	// compute the color output resistor weights
	double rweights[3], gweights[3], bweights[2];
	compute_resistor_weights(0, 255, -1.0,
		3, &resistances[0], rweights, 0, 0,
		3, &resistances[0], gweights, 0, 0,
		2, &resistances[1], bweights, 0, 0);

	for (int i = 0; i < 0x100; i++)
	{
		bool bit0, bit1, bit2;

		// red component
		bit0 = BIT(prom[i], 7);
		bit1 = BIT(prom[i], 6);
		bit2 = BIT(prom[i], 5);
		u8 const r = combine_weights(rweights, bit0, bit1, bit2);

		// green component
		bit0 = BIT(prom[i], 4);
		bit1 = BIT(prom[i], 3);
		bit2 = BIT(prom[i], 2);
		u8 const g = combine_weights(gweights, bit0, bit1, bit2);

		// blue component
		bit0 = BIT(prom[i], 1);
		bit1 = BIT(prom[i], 0);
		u8 const b = combine_weights(bweights, bit0, bit1);

		// because the graphics are decoded as 4bpp with the top bit used for transparency
		// or priority, we create matching 3bpp sets of palette entries, which effectively
		// ignores the value of the top bit
		palette.set_pen_color(((i & 0xf8) << 1) | 0x00 | (i & 0x07), rgb_t(r, g, b));
		palette.set_pen_color(((i & 0xf8) << 1) | 0x08 | (i & 0x07), rgb_t(r, g, b));
	}
}

void docastle_state::videoram_w(offs_t offset, u8 data)
{
	m_videoram[offset] = data;
	m_tilemap->mark_tile_dirty(offset);
}

void docastle_state::colorram_w(offs_t offset, u8 data)
{
	m_colorram[offset] = data;
	m_tilemap->mark_tile_dirty(offset);
}

u8 docastle_state::inputs_flipscreen_r(offs_t offset)
{
	u8 buf = 0xff;

	if (!machine().side_effects_disabled())
	{
		// inputs pass through LS244 non-inverting buffer
		buf = (m_inp[1]->read_h() << 4) | m_inp[0]->read_h();

		// LS273 latches address bits on rising edge of address decode
		flip_screen_set(BIT(offset, 7));
		m_inp[0]->write_s(offset & 7);
		m_inp[1]->write_s(offset & 7);
	}

	return buf;
}

void docastle_state::flipscreen_w(offs_t offset, u8 data)
{
	flip_screen_set(BIT(offset, 7));
}

TILE_GET_INFO_MEMBER(docastle_state::get_tile_info)
{
	u16 const code = m_videoram[tile_index] | (BIT(m_colorram[tile_index], 5) << 8);
	u8 const color = m_colorram[tile_index] & 0x1f;

	tileinfo.set(0, code, color, 0);
}

void docastle_state::video_start()
{
	m_tilemap = &machine().tilemap().create(*m_gfxdecode, tilemap_get_info_delegate(*this, FUNC(docastle_state::get_tile_info)), TILEMAP_SCAN_ROWS, 8, 8, 32, 32);
	m_tilemap->set_scrolldy(-32, -32);
	m_tilemap->set_transmask(0, m_tile_mask, 0x0000);
}

void idsoccer_state::extend_sprite(bool &flipy, u16 &code, u8 &color)
{
	code  |= BIT(color, 4) << 8;
	color &= ~(1 << 4);

	code |= flipy << 9;
	flipy = false;
}

void docastle_state::draw_sprites(screen_device &screen, bitmap_rgb32 &bitmap, rectangle const &cliprect)
{
	screen.priority().fill(1);

	for (int offs = m_spriteram.bytes() - 4; offs >= 0; offs -= 4)
	{
		/*
			 yyyy yyyy  xxxx xxxx  YX-p pppp  tttt tttt

			 y = ypos
			 x = xpos
			 X = x-flip
			 Y = y-flip
			 p = palette
			 t = tile number
		*/
		u8 const *src  = &m_spriteram[offs];
		int xpos       = ((src[offs + 1] + 8) & 0xff) - 8;
		int ypos       = src[offs] - 32;
		u8 const attr  = src[offs + 2];
		bool flipx     = BIT(attr, 6);
		bool flipy     = BIT(attr, 7);
		u8 color       = attr & 0x1f;
		u16 code       = src[offs + 3];

		//extend_sprite(flipy, code, color);

		if (flip_screen())
		{
			xpos = 240 - xpos;
			ypos = 176 - ypos;
			flipx = !flipx;
			flipy = !flipy;
		}

		// first draw the sprite, visible
		m_gfxdecode->gfx(1)->prio_transmask(bitmap, cliprect,
			code, color, flipx, flipy, xpos, ypos,
			screen.priority(), 0x00, 0x80ff);

		// then draw the mask, behind the background but obscuring following sprites
		m_gfxdecode->gfx(1)->prio_transmask(bitmap,cliprect,
			code, color, flipx, flipy, xpos, ypos,
			screen.priority(), 0x02, 0x7fff);
	}
}

u32 docastle_state::screen_update(screen_device &screen, bitmap_rgb32 &bitmap, rectangle const &cliprect)
{
	m_tilemap->draw(screen, bitmap, cliprect, TILEMAP_DRAW_OPAQUE, 0);
	draw_sprites(screen, bitmap, cliprect);
	m_tilemap->draw(screen, bitmap, cliprect, TILEMAP_DRAW_LAYER0, 0);
	return 0;
}

// Read/Write Handlers
/*
THIS IS A GIANT HACK! It can be accurately emulated once our Z80 core supports WAIT.

Communication between the two CPUs happens through a single bidirectional latch.
Whenever CPU 0 reads or writes it, its WAIT input is asserted. It is implicitly
cleared by CPU 1, when it accesses the latch. This enforces synchronization
between the two CPUs.
This is currently impossible to reproduce accurately in MAME, because it would
require putting on hold CPU 0 while it is reading the latch, and resume its
execution only when CPU 1 has written it.
Instead, we take advantage of how the two CPUs access the latch, and treat it as
if it was a small shared buffer. The order of operations is:
1) CPU 0 triggers NMI on CPU 1
2) CPU 0 writes 9 bytes to the buffer
3) at this point we suspend execution of CPU 0, to give CPU 1 time to read the 9
   bytes and write its own 9 bytes
4) resume execution of CPU 0.
*/
#define LOG 0
u8 docastle_state::cpu1_latch_r(offs_t offset)
{
#if USE_LATCH
	m_cpu[0]->set_input_line(Z80_INPUT_LINE_WAIT, ASSERT_LINE);
	return m_latch;
#else
	if (offset == 8 && LOG)
		logerror("CPU #0 shared0r  clock = %d\n", (uint32_t)m_cpu[0]->total_cycles());

	return m_buffer0[offset];
#endif
}

void docastle_state::cpu1_latch_w(offs_t offset, u8 data)
{
#if USE_LATCH
	m_cpu[0]->set_input_line(Z80_INPUT_LINE_WAIT, ASSERT_LINE);
	m_latch = data;
#else
	m_buffer1[offset] = data;

	if (offset == 8 && LOG)
		logerror("CPU #0 shared1w %02x %02x %02x %02x %02x %02x %02x %02x %02x clock = %d\n",
		m_buffer1[0], m_buffer1[1], m_buffer1[2], m_buffer1[3],
		m_buffer1[4], m_buffer1[5], m_buffer1[6], m_buffer1[7],
		data, (uint32_t)m_cpu[0]->total_cycles());

	/* freeze execution of the master CPU until the slave has used the shared memory */
	if (offset == 8)
		m_cpu[0]->spin_until_trigger(500);
#endif
}

u8 docastle_state::cpu2_latch_r(offs_t offset)
{
#if USE_LATCH
	m_cpu[0]->set_input_line(Z80_INPUT_LINE_WAIT, CLEAR_LINE);
	return m_latch;
#else
	if (offset == 8 && LOG)
		logerror("CPU #1 shared1r  clock = %d\n", (uint32_t)m_cpu[1]->total_cycles());

	return m_buffer1[offset];
#endif
}

void docastle_state::cpu2_latch_w(offs_t offset, u8 data)
{
#if USE_LATCH
	m_cpu[0]->set_input_line(Z80_INPUT_LINE_WAIT, CLEAR_LINE);
	m_latch = data;
#else
	if (offset == 8 && LOG)
		logerror("CPU #1 shared0w %02x %02x %02x %02x %02x %02x %02x %02x %02x clock = %d\n",
		m_buffer0[0], m_buffer0[1], m_buffer0[2], m_buffer0[3],
		m_buffer0[4], m_buffer0[5], m_buffer0[6], m_buffer0[7],
		data, (uint32_t)m_cpu[1]->total_cycles());

	m_buffer0[offset] = data;

	/* awake the master CPU */
	if (offset == 8)
		machine().scheduler().trigger(500);
#endif
}

void docastle_state::nmi_trigger_w(u8 data)
{
	m_cpu[1]->pulse_input_line(INPUT_LINE_NMI, attotime::zero);
}

void docastle_state::tint(int state)
{
	if (state)
	{
		bool const ma6 = BIT(m_crtc->get_ma(), 6);
		if (ma6 && !m_prev_ma6) // MA6 rising edge
			m_cpu[1]->set_input_line(INPUT_LINE_IRQ0, HOLD_LINE);
		m_prev_ma6 = ma6;
	}
}

void idsoccer_state::adpcm_int(int state)
{
	if (m_adpcm_pos >= m_adpcm.bytes())
	{
		m_adpcm_idle = 1;
		m_msm->reset_w(1);
	}
	else if (m_adpcm_data != -1)
	{
		m_msm->data_w(m_adpcm_data & 0x0f);
		m_adpcm_data = -1;
	}
	else
	{
		m_adpcm_data = m_adpcm[m_adpcm_pos++];
		m_msm->data_w(m_adpcm_data >> 4);
	}
}

u8 idsoccer_state::adpcm_status_r()
{
	// this is wrong, but the samples work anyway!!
	m_adpcm_status ^= 0x80;
	return m_adpcm_status;
}

void idsoccer_state::adpcm_w(u8 data)
{
	if (BIT(data, 7))
	{
		m_adpcm_idle = 1;
		m_msm->reset_w(1);
	}
	else
	{
		m_adpcm_pos = (data & 0x7f) * 0x200;
		m_adpcm_idle = 0;
		m_msm->reset_w(0);
	}
}

// Memory Maps
void docastle_state::docastle_map1(address_map &map)
{
	map(0x0000, 0x7fff).rom();
	map(0x8000, 0x97ff).ram();
	map(0x9800, 0x99ff).ram().share(m_spriteram);
	map(0xa000, 0xa008).rw(FUNC(docastle_state::cpu1_latch_r), FUNC(docastle_state::cpu1_latch_w));
	map(0xa800, 0xa800).w(m_watchdog, FUNC(watchdog_timer_device::reset_w));
	map(0xb000, 0xb3ff).mirror(0x0800).ram().w(FUNC(docastle_state::videoram_w)).share(m_videoram);
	map(0xb400, 0xb7ff).mirror(0x0800).ram().w(FUNC(docastle_state::colorram_w)).share(m_colorram);
	map(0xe000, 0xe000).w(FUNC(docastle_state::nmi_trigger_w));
}

void docastle_state::docastle_map2(address_map &map)
{
	map(0x0000, 0x3fff).rom();
	map(0x8000, 0x87ff).ram();
	map(0xa000, 0xa008).rw(FUNC(docastle_state::cpu2_latch_r), FUNC(docastle_state::cpu2_latch_w));
	map(0xc000, 0xc007).select(0x0080).rw(FUNC(docastle_state::inputs_flipscreen_r), FUNC(docastle_state::flipscreen_w));
	map(0xe000, 0xe000).w(m_sn76489[0], FUNC(sn76489a_device::write));
	map(0xe400, 0xe400).w(m_sn76489[1], FUNC(sn76489a_device::write));
	map(0xe800, 0xe800).w(m_sn76489[2], FUNC(sn76489a_device::write));
	map(0xec00, 0xec00).w(m_sn76489[3], FUNC(sn76489a_device::write));
}

void docastle_state::docastle_map3(address_map &map)
{
	map(0x0000, 0x00ff).rom();
	map(0x4000, 0x47ff).ram();
	map(0x8000, 0x8000).nopr(); // latch from cpu1
	map(0xc000, 0xc7ff).noprw(); // sprite chip?
}

void docastle_state::docastle_io_map(address_map &map)
{
	map.global_mask(0xff);
	map(0x00, 0x00).w(m_crtc, FUNC(mc6845_device::address_w));
	map(0x02, 0x02).w(m_crtc, FUNC(mc6845_device::register_w));
}

void docastle_state::dorunrun_map1(address_map &map)
{
	map(0x0000, 0x1fff).rom();
	map(0x2000, 0x37ff).ram();
	map(0x3800, 0x39ff).ram().share(m_spriteram);
	map(0x4000, 0x9fff).rom();
	map(0xa000, 0xa008).rw(FUNC(docastle_state::cpu1_latch_r), FUNC(docastle_state::cpu1_latch_w));
	map(0xa800, 0xa800).w(m_watchdog, FUNC(watchdog_timer_device::reset_w));
	map(0xb000, 0xb3ff).ram().w(FUNC(docastle_state::videoram_w)).share(m_videoram);
	map(0xb400, 0xb7ff).ram().w(FUNC(docastle_state::colorram_w)).share(m_colorram);
	map(0xb800, 0xb800).w(FUNC(docastle_state::nmi_trigger_w));
}

void docastle_state::dorunrun_map2(address_map &map)
{
	map(0x0000, 0x3fff).rom();
	map(0x8000, 0x87ff).ram();
	map(0xa000, 0xa000).w(m_sn76489[0], FUNC(sn76489a_device::write));
	map(0xa400, 0xa400).w(m_sn76489[1], FUNC(sn76489a_device::write));
	map(0xa800, 0xa800).w(m_sn76489[2], FUNC(sn76489a_device::write));
	map(0xac00, 0xac00).w(m_sn76489[3], FUNC(sn76489a_device::write));
	map(0xc000, 0xc007).select(0x0080).rw(FUNC(docastle_state::inputs_flipscreen_r), FUNC(docastle_state::flipscreen_w));
	map(0xe000, 0xe008).rw(FUNC(docastle_state::cpu2_latch_r), FUNC(docastle_state::cpu2_latch_w));
}

void idsoccer_state::idsoccer_map1(address_map &map)
{
	map(0x0000, 0x3fff).rom();
	map(0x4000, 0x57ff).ram();
	map(0x5800, 0x59ff).ram().share(m_spriteram);
	map(0x6000, 0x9fff).rom();
	map(0xa000, 0xa008).rw(FUNC(idsoccer_state::cpu1_latch_r), FUNC(idsoccer_state::cpu1_latch_w));
	map(0xa800, 0xa800).w(m_watchdog, FUNC(watchdog_timer_device::reset_w));
	map(0xb000, 0xb3ff).mirror(0x0800).ram().w(FUNC(idsoccer_state::videoram_w)).share(m_videoram);
	map(0xb400, 0xb7ff).mirror(0x0800).ram().w(FUNC(idsoccer_state::colorram_w)).share(m_colorram);
	map(0xc000, 0xc000).rw(FUNC(idsoccer_state::adpcm_status_r), FUNC(idsoccer_state::adpcm_w));
	map(0xe000, 0xe000).w(FUNC(idsoccer_state::nmi_trigger_w));
}

// Input Ports
static INPUT_PORTS_START( docastle )
	PORT_START("JOYS")
	PORT_BIT( 0x01, IP_ACTIVE_LOW, IPT_JOYSTICK_RIGHT ) PORT_4WAY
	PORT_BIT( 0x02, IP_ACTIVE_LOW, IPT_JOYSTICK_UP )    PORT_4WAY
	PORT_BIT( 0x04, IP_ACTIVE_LOW, IPT_JOYSTICK_LEFT )  PORT_4WAY
	PORT_BIT( 0x08, IP_ACTIVE_LOW, IPT_JOYSTICK_DOWN )  PORT_4WAY
	PORT_BIT( 0x10, IP_ACTIVE_LOW, IPT_JOYSTICK_RIGHT ) PORT_4WAY PORT_COCKTAIL
	PORT_BIT( 0x20, IP_ACTIVE_LOW, IPT_JOYSTICK_UP )    PORT_4WAY PORT_COCKTAIL
	PORT_BIT( 0x40, IP_ACTIVE_LOW, IPT_JOYSTICK_LEFT )  PORT_4WAY PORT_COCKTAIL
	PORT_BIT( 0x80, IP_ACTIVE_LOW, IPT_JOYSTICK_DOWN )  PORT_4WAY PORT_COCKTAIL

	PORT_START("BUTTONS")
	PORT_BIT( 0x01, IP_ACTIVE_LOW, IPT_BUTTON1 )
	PORT_BIT( 0x02, IP_ACTIVE_LOW, IPT_BUTTON2 )
	PORT_BIT( 0x04, IP_ACTIVE_LOW, IPT_UNUSED )
	PORT_BIT( 0x08, IP_ACTIVE_LOW, IPT_START1 )
	PORT_BIT( 0x10, IP_ACTIVE_LOW, IPT_BUTTON1 ) PORT_COCKTAIL
	PORT_BIT( 0x20, IP_ACTIVE_LOW, IPT_BUTTON2 ) PORT_COCKTAIL
	PORT_BIT( 0x40, IP_ACTIVE_LOW, IPT_UNUSED )
	PORT_BIT( 0x80, IP_ACTIVE_LOW, IPT_START2 )

	PORT_START("SYSTEM")
	PORT_BIT( 0x01, IP_ACTIVE_LOW, IPT_TILT )
	PORT_SERVICE_NO_TOGGLE( 0x02, IP_ACTIVE_LOW )
	PORT_BIT( 0x04, IP_ACTIVE_LOW, IPT_SERVICE1 ) PORT_IMPULSE(32)
	PORT_DIPNAME( 0x08, 0x08, "Freeze" )
	PORT_DIPSETTING(    0x08, DEF_STR( Off ) )
	PORT_DIPSETTING(    0x00, DEF_STR( On ) )
	PORT_BIT( 0x10, IP_ACTIVE_LOW, IPT_COIN2 )
	PORT_BIT( 0x20, IP_ACTIVE_LOW, IPT_COIN1 )
	PORT_BIT( 0xc0, IP_ACTIVE_LOW, IPT_UNUSED )

	PORT_START("DSW1")
	PORT_DIPNAME( 0x03, 0x03, DEF_STR( Difficulty ) )              PORT_DIPLOCATION("SWA:8,7")
	PORT_DIPSETTING(    0x03, "1 (Beginner)" )
	PORT_DIPSETTING(    0x02, "2" )
	PORT_DIPSETTING(    0x01, "3" )
	PORT_DIPSETTING(    0x00, "4 (Advanced)" )
	PORT_DIPNAME( 0x04, 0x04, "Rack Test" )                        PORT_DIPLOCATION("SWA:6")
	PORT_DIPSETTING(    0x04, DEF_STR( Off ) )
	PORT_DIPSETTING(    0x00, DEF_STR( On ) )
	PORT_DIPNAME( 0x08, 0x08, "Advance Level on Getting Diamond" ) PORT_DIPLOCATION("SWA:5")
	PORT_DIPSETTING(    0x08, DEF_STR( Off ) )
	PORT_DIPSETTING(    0x00, DEF_STR( On ) )
	PORT_DIPNAME( 0x10, 0x10, "Difficulty of EXTRA" )              PORT_DIPLOCATION("SWA:4")
	PORT_DIPSETTING(    0x10, DEF_STR( Easy ) )
	PORT_DIPSETTING(    0x00, DEF_STR( Difficult ) )
	PORT_DIPNAME( 0x20, 0x00, DEF_STR( Cabinet ) )                 PORT_DIPLOCATION("SWA:3")
	PORT_DIPSETTING(    0x00, DEF_STR( Upright ) )
	PORT_DIPSETTING(    0x20, DEF_STR( Cocktail ) )
	PORT_DIPNAME( 0xc0, 0xc0, DEF_STR( Lives ) )                   PORT_DIPLOCATION("SWA:2,1")
	PORT_DIPSETTING(    0x00, "2" )
	PORT_DIPSETTING(    0xc0, "3" )
	PORT_DIPSETTING(    0x80, "4" )
	PORT_DIPSETTING(    0x40, "5" )

	PORT_START("DSW2")
	PORT_DIPNAME( 0x0f, 0x0f, DEF_STR( Coin_B ) )                  PORT_DIPLOCATION("SWB:8,7,6,5")
	PORT_DIPSETTING(    0x06, DEF_STR( 4C_1C ) )
	PORT_DIPSETTING(    0x08, DEF_STR( 3C_1C ) )
	PORT_DIPSETTING(    0x0a, DEF_STR( 2C_1C ) )
	PORT_DIPSETTING(    0x07, DEF_STR( 3C_2C ) )
	PORT_DIPSETTING(    0x0f, DEF_STR( 1C_1C ) )
	PORT_DIPSETTING(    0x09, DEF_STR( 2C_3C ) )
	PORT_DIPSETTING(    0x0e, DEF_STR( 1C_2C ) )
	PORT_DIPSETTING(    0x0d, DEF_STR( 1C_3C ) )
	PORT_DIPSETTING(    0x0c, DEF_STR( 1C_4C ) )
	PORT_DIPSETTING(    0x0b, DEF_STR( 1C_5C ) )
	PORT_DIPSETTING(    0x00, DEF_STR( Free_Play ) )
	// 0x01, 0x02, 0x03, 0x04, 0x05 all give 1 Coin/1 Credit
	PORT_DIPNAME( 0xf0, 0xf0, DEF_STR( Coin_A ) )                  PORT_DIPLOCATION("SWB:4,3,2,1")
	PORT_DIPSETTING(    0x60, DEF_STR( 4C_1C ) )
	PORT_DIPSETTING(    0x80, DEF_STR( 3C_1C ) )
	PORT_DIPSETTING(    0xa0, DEF_STR( 2C_1C ) )
	PORT_DIPSETTING(    0x70, DEF_STR( 3C_2C ) )
	PORT_DIPSETTING(    0xf0, DEF_STR( 1C_1C ) )
	PORT_DIPSETTING(    0x90, DEF_STR( 2C_3C ) )
	PORT_DIPSETTING(    0xe0, DEF_STR( 1C_2C ) )
	PORT_DIPSETTING(    0xd0, DEF_STR( 1C_3C ) )
	PORT_DIPSETTING(    0xc0, DEF_STR( 1C_4C ) )
	PORT_DIPSETTING(    0xb0, DEF_STR( 1C_5C ) )
	PORT_DIPSETTING(    0x00, DEF_STR( Free_Play ) )
	// 0x10, 0x20, 0x30, 0x40, 0x50 all give 1 Coin/1 Credit
INPUT_PORTS_END

static INPUT_PORTS_START( dorunrun )
	PORT_INCLUDE( docastle )

	PORT_MODIFY("DSW1")
	PORT_DIPNAME( 0x04, 0x04, DEF_STR( Demo_Sounds ) ) PORT_DIPLOCATION("SWA:6")
	PORT_DIPSETTING(    0x00, DEF_STR( Off ) )
	PORT_DIPSETTING(    0x04, DEF_STR( On ) )
	PORT_DIPNAME( 0x08, 0x08, DEF_STR( Flip_Screen ) ) PORT_DIPLOCATION("SWA:5")
	PORT_DIPSETTING(    0x08, DEF_STR( Off ) )
	PORT_DIPSETTING(    0x00, DEF_STR( On ) )
	PORT_DIPNAME( 0x40, 0x40, "Special" ) PORT_DIPLOCATION("SWA:2")
	PORT_DIPSETTING(    0x40, "Given" )
	PORT_DIPSETTING(    0x00, "Not Given" )
	PORT_DIPNAME( 0x80, 0x80, DEF_STR( Lives ) ) PORT_DIPLOCATION("SWA:1")
	PORT_DIPSETTING(    0x80, "3" )
	PORT_DIPSETTING(    0x00, "5" )
INPUT_PORTS_END

static INPUT_PORTS_START( runrun )
	PORT_INCLUDE( dorunrun )

	PORT_MODIFY("DSW1")
	PORT_DIPNAME( 0x80, 0x80, DEF_STR( Lives ) ) PORT_DIPLOCATION("SWA:1")
	PORT_DIPSETTING(    0x80, "1" )
	PORT_DIPSETTING(    0x00, "2" )
INPUT_PORTS_END

static INPUT_PORTS_START( dowild )
	PORT_INCLUDE( docastle )

	PORT_MODIFY("DSW1")
	PORT_DIPNAME( 0x08, 0x08, DEF_STR( Flip_Screen ) ) PORT_DIPLOCATION("SWA:5")
	PORT_DIPSETTING(    0x08, DEF_STR( Off ) )
	PORT_DIPSETTING(    0x00, DEF_STR( On ) )
	PORT_DIPNAME( 0x40, 0x40, "Special" ) PORT_DIPLOCATION("SWA:2")
	PORT_DIPSETTING(    0x40, "Given" )
	PORT_DIPSETTING(    0x00, "Not Given" )
	PORT_DIPNAME( 0x80, 0x80, DEF_STR( Lives ) ) PORT_DIPLOCATION("SWA:1")
	PORT_DIPSETTING(    0x80, "3" )
	PORT_DIPSETTING(    0x00, "5" )
INPUT_PORTS_END

static INPUT_PORTS_START( jjack )
	PORT_INCLUDE( docastle )

	PORT_MODIFY("DSW1")
	PORT_DIPNAME( 0x03, 0x03, "Difficulty?" )
	PORT_DIPSETTING(    0x03, DEF_STR( Easy ) )
	PORT_DIPSETTING(    0x02, DEF_STR( Medium ) )
	PORT_DIPSETTING(    0x01, DEF_STR( Hard ) )
	PORT_DIPSETTING(    0x00, DEF_STR( Hardest ) )
	PORT_DIPNAME( 0x08, 0x08, DEF_STR( Flip_Screen ) )
	PORT_DIPSETTING(    0x08, DEF_STR( Off ) )
	PORT_DIPSETTING(    0x00, DEF_STR( On ) )
	PORT_DIPNAME( 0x10, 0x10, "Extra?" )
	PORT_DIPSETTING(    0x10, DEF_STR( Easy ) )
	PORT_DIPSETTING(    0x00, DEF_STR( Hard ) )
INPUT_PORTS_END

static INPUT_PORTS_START( kickridr )
	PORT_INCLUDE( docastle )

	PORT_MODIFY("DSW1")
	PORT_DIPNAME( 0x03, 0x03, "Difficulty?" )
	PORT_DIPSETTING(    0x03, DEF_STR( Easy ) )
	PORT_DIPSETTING(    0x02, DEF_STR( Medium ) )
	PORT_DIPSETTING(    0x01, DEF_STR( Hard ) )
	PORT_DIPSETTING(    0x00, DEF_STR( Hardest ) )
	PORT_DIPNAME( 0x08, 0x08, DEF_STR( Flip_Screen ) )
	PORT_DIPSETTING(    0x08, DEF_STR( Off ) )
	PORT_DIPSETTING(    0x00, DEF_STR( On ) )
	PORT_DIPNAME( 0x10, 0x10, "DSW4" )
	PORT_DIPSETTING(    0x10, DEF_STR( Off ) )
	PORT_DIPSETTING(    0x00, DEF_STR( On ) )
	PORT_DIPNAME( 0x40, 0x40, "DSW2" )
	PORT_DIPSETTING(    0x40, DEF_STR( Off ) )
	PORT_DIPSETTING(    0x00, DEF_STR( On ) )
	PORT_DIPNAME( 0x80, 0x80, "DSW1" )
	PORT_DIPSETTING(    0x00, DEF_STR( Off ) )
	PORT_DIPSETTING(    0x80, DEF_STR( On ) )
INPUT_PORTS_END

static INPUT_PORTS_START( idsoccer )
	PORT_INCLUDE( docastle )

	PORT_MODIFY("JOYS")
	PORT_BIT( 0x01, IP_ACTIVE_LOW, IPT_JOYSTICKLEFT_RIGHT ) PORT_8WAY
	PORT_BIT( 0x02, IP_ACTIVE_LOW, IPT_JOYSTICKLEFT_UP ) PORT_8WAY
	PORT_BIT( 0x04, IP_ACTIVE_LOW, IPT_JOYSTICKLEFT_LEFT ) PORT_8WAY
	PORT_BIT( 0x08, IP_ACTIVE_LOW, IPT_JOYSTICKLEFT_DOWN ) PORT_8WAY
	PORT_BIT( 0x10, IP_ACTIVE_LOW, IPT_JOYSTICKLEFT_RIGHT ) PORT_8WAY PORT_PLAYER(2)
	PORT_BIT( 0x20, IP_ACTIVE_LOW, IPT_JOYSTICKLEFT_UP ) PORT_8WAY PORT_PLAYER(2)
	PORT_BIT( 0x40, IP_ACTIVE_LOW, IPT_JOYSTICKLEFT_LEFT ) PORT_8WAY PORT_PLAYER(2)
	PORT_BIT( 0x80, IP_ACTIVE_LOW, IPT_JOYSTICKLEFT_DOWN ) PORT_8WAY PORT_PLAYER(2)

	PORT_MODIFY("DSW1")
	PORT_DIPNAME( 0x03, 0x03, DEF_STR( Difficulty ) ) PORT_DIPLOCATION("SW1:7,8")
	PORT_DIPSETTING(    0x03, DEF_STR( Easy ) )
	PORT_DIPSETTING(    0x02, DEF_STR( Medium ) )
	PORT_DIPSETTING(    0x01, DEF_STR( Hard ) )
	PORT_DIPSETTING(    0x00, DEF_STR( Hardest ) )
	PORT_DIPNAME( 0x04, 0x04, "One Player vs. Computer" ) PORT_DIPLOCATION("SW1:!6")    // Additional time extended for winning score
	PORT_DIPSETTING(    0x04, DEF_STR( Off ) )
	PORT_DIPSETTING(    0x00, DEF_STR( On ) )
	PORT_DIPNAME( 0x08, 0x08, DEF_STR( Flip_Screen ) ) PORT_DIPLOCATION("SW1:5")
	PORT_DIPSETTING(    0x08, DEF_STR( Off ) )
	PORT_DIPSETTING(    0x00, DEF_STR( On ) )
	PORT_DIPNAME( 0x10, 0x10, "Player 2 Time Extension" ) PORT_DIPLOCATION("SW1:4") // Player may play same game with additional credit
	PORT_DIPSETTING(    0x00, DEF_STR( Off ) )
	PORT_DIPSETTING(    0x10, DEF_STR( On ) )
	PORT_DIPNAME( 0x20, 0x20, "Player 1 Time Extension" ) PORT_DIPLOCATION("SW1:3") // Player may play same game with additional credit
	PORT_DIPSETTING(    0x00, DEF_STR( Off ) )
	PORT_DIPSETTING(    0x20, DEF_STR( On ) )
	PORT_DIPNAME( 0xc0, 0xc0, "Real Game Time" ) PORT_DIPLOCATION("SW1:1,2") // Indicator always shows 3:00 and counts down
	PORT_DIPSETTING(    0xc0, "3:00" )
	PORT_DIPSETTING(    0x80, "2:30" )
	PORT_DIPSETTING(    0x40, "2:00" )
	PORT_DIPSETTING(    0x00, "1:00" )

	PORT_START("JOYS_RIGHT")
	PORT_BIT( 0x01, IP_ACTIVE_LOW, IPT_JOYSTICKRIGHT_RIGHT ) PORT_8WAY
	PORT_BIT( 0x02, IP_ACTIVE_LOW, IPT_JOYSTICKRIGHT_UP )    PORT_8WAY
	PORT_BIT( 0x04, IP_ACTIVE_LOW, IPT_JOYSTICKRIGHT_LEFT )  PORT_8WAY
	PORT_BIT( 0x08, IP_ACTIVE_LOW, IPT_JOYSTICKRIGHT_DOWN )  PORT_8WAY
	PORT_BIT( 0x10, IP_ACTIVE_LOW, IPT_JOYSTICKRIGHT_RIGHT ) PORT_8WAY PORT_PLAYER(2)
	PORT_BIT( 0x20, IP_ACTIVE_LOW, IPT_JOYSTICKRIGHT_UP )    PORT_8WAY PORT_PLAYER(2)
	PORT_BIT( 0x40, IP_ACTIVE_LOW, IPT_JOYSTICKRIGHT_LEFT )  PORT_8WAY PORT_PLAYER(2)
	PORT_BIT( 0x80, IP_ACTIVE_LOW, IPT_JOYSTICKRIGHT_DOWN )  PORT_8WAY PORT_PLAYER(2)
INPUT_PORTS_END

// Graphics Decode Information

static GFXDECODE_START( gfx_docastle )
	GFXDECODE_ENTRY( "gfx8x8",   0, gfx_8x8x4_packed_msb,   0, 64   )
	GFXDECODE_ENTRY( "gfx16x16", 0, gfx_16x16x4_packed_msb, 0, 32*2 )
GFXDECODE_END


// Machine Drivers

void docastle_state::machine_start()
{
	save_item(NAME(m_prev_ma6));
#if USE_LATCH
#else
	save_item(NAME(m_buffer0));
	save_item(NAME(m_buffer1));
#endif
}

void docastle_state::machine_reset()
{
#if USE_LATCH
#else
	std::fill(std::begin(m_buffer0), std::end(m_buffer0), 0);
	std::fill(std::begin(m_buffer1), std::end(m_buffer1), 0);
#endif

	m_prev_ma6 = false;
}

void docastle_state::video_reset()
{
	flip_screen_set(false); // cleared with LS273
}

void idsoccer_state::machine_start()
{
	docastle_state::machine_start();

	save_item(NAME(m_adpcm_pos));
	save_item(NAME(m_adpcm_data));
	save_item(NAME(m_adpcm_idle));
	save_item(NAME(m_adpcm_status));
}

void idsoccer_state::machine_reset()
{
	docastle_state::machine_reset();

	m_adpcm_pos = 0;
	m_adpcm_idle = 0;
	m_adpcm_data = -1;
	m_adpcm_status = 0;
}

void docastle_state::docastle(machine_config &config)
{
	XTAL constexpr MASTER_CLOCK = 4_MHz_XTAL;
	XTAL constexpr VIDEO_CLOCK = 9.828_MHz_XTAL;

	// basic machine hardware
	for (auto &cpu : m_cpu)
		Z80(config, cpu, MASTER_CLOCK);
	m_cpu[0]->set_addrmap(AS_PROGRAM, &docastle_state::docastle_map1);
	m_cpu[1]->set_addrmap(AS_PROGRAM, &docastle_state::docastle_map2);
	m_cpu[2]->set_addrmap(AS_PROGRAM, &docastle_state::docastle_map3);
	m_cpu[0]->set_addrmap(AS_IO, &docastle_state::docastle_io_map);

#if USE_LATCH
	config.set_perfect_quantum(m_cpu[0]);
#endif

	for (auto &inp : m_inp)
		TMS1025(config, inp);
	m_inp[0]->read_port1_callback().set_ioport("DSW2");
	m_inp[1]->read_port1_callback().set_ioport("DSW2").rshift(4);
	m_inp[0]->read_port2_callback().set_ioport("DSW1");
	m_inp[1]->read_port2_callback().set_ioport("DSW1").rshift(4);
	m_inp[0]->read_port3_callback().set_ioport("JOYS");
	m_inp[1]->read_port3_callback().set_ioport("JOYS").rshift(4);
	m_inp[0]->read_port5_callback().set_ioport("BUTTONS");
	m_inp[1]->read_port5_callback().set_ioport("BUTTONS").rshift(4);
	m_inp[0]->read_port7_callback().set_ioport("SYSTEM");
	m_inp[1]->read_port7_callback().set_ioport("SYSTEM").rshift(4);

	WATCHDOG_TIMER(config, m_watchdog);

	// video hardware
	SCREEN(config, m_screen, SCREEN_TYPE_RASTER);
	m_screen->set_raw(VIDEO_CLOCK / 2, 0x138, 8, 0x100 - 8, 0x108, 0, 0xc0); // from CRTC
	m_screen->set_screen_update(FUNC(docastle_state::screen_update));

	HD6845S(config, m_crtc, VIDEO_CLOCK / 16);
	/*
	The games program the CRTC for a width of 32 characters (256 pixels).
	However, the DE output from the CRTC is first ANDed with the NAND of
	MA1 through MA4, and then delayed by 8 pixel clocks; this effectively
	blanks the first 8 pixels and last 8 pixels of each line.
	*/
	m_crtc->set_screen(m_screen);
	m_crtc->set_show_border_area(false);
	m_crtc->set_visarea_adjust(8, -8, 0, 0);
	m_crtc->set_char_width(8);
	m_crtc->out_vsync_callback().set_inputline(m_cpu[0], INPUT_LINE_IRQ0);
	m_crtc->out_vsync_callback().append_inputline(m_cpu[2], INPUT_LINE_NMI);
	m_crtc->out_hsync_callback().set(FUNC(docastle_state::tint));

	GFXDECODE(config, m_gfxdecode, m_palette, gfx_docastle);
	PALETTE(config, m_palette, FUNC(docastle_state::palette), 512);

	// sound hardware
	SPEAKER(config, "mono").front_center();

	for (auto &sn : m_sn76489)
		SN76489A(config, sn, MASTER_CLOCK).add_route(ALL_OUTPUTS, "mono", 0.25);
}

void docastle_state::dorunrun(machine_config &config)
{
	docastle(config);

	// basic machine hardware
	m_cpu[0]->set_addrmap(AS_PROGRAM, &docastle_state::dorunrun_map1);
	m_cpu[1]->set_addrmap(AS_PROGRAM, &docastle_state::dorunrun_map2);

	// video hardware
	set_tile_mask(0xff00);
}

void idsoccer_state::idsoccer(machine_config &config)
{
	docastle(config);

	// basic machine hardware
	m_cpu[0]->set_addrmap(AS_PROGRAM, &idsoccer_state::idsoccer_map1);

	m_inp[0]->read_port4_callback().set_ioport("JOYS_RIGHT");
	m_inp[1]->read_port4_callback().set_ioport("JOYS_RIGHT").rshift(4);

	// video hardware
	set_tile_mask(0xff00);

	// sound hardware
	MSM5205(config, m_msm, 384_kHz_XTAL); // Crystal verified on American Soccer board.
	m_msm->vck_legacy_callback().set(FUNC(idsoccer_state::adpcm_int)); // interrupt function
	m_msm->set_prescaler_selector(msm5205_device::S64_4B); // 6 kHz ???
	m_msm->add_route(ALL_OUTPUTS, "mono", 0.40);
}

// ROMs

ROM_START( docastle )
	ROM_REGION( 0x10000, "cpu1", 0 )
	ROM_LOAD( "01p_a1.bin",   0x0000, 0x2000, CRC(17c6fc24) SHA1(e397d283e8b8e1c512495b777c0fe16f66bb1862) )
	ROM_LOAD( "01n_a2.bin",   0x2000, 0x2000, CRC(1d2fc7f4) SHA1(f7b0c7466cd6a3854eda818c63663e3559dc7bc2) )
	ROM_LOAD( "01l_a3.bin",   0x4000, 0x2000, CRC(71a70ba9) SHA1(0c9e4c402f82d61d573eb55b3e2e0c8b7c8b7028) )
	ROM_LOAD( "01k_a4.bin",   0x6000, 0x2000, CRC(479a745e) SHA1(7195036727990932bc94b30405ebc2b8ea5b37a8) )

	ROM_REGION( 0x10000, "cpu2", 0 )
	ROM_LOAD( "07n_a0.bin",   0x0000, 0x4000, CRC(f23b5cdb) SHA1(de71d9f142f8d420aa097d7e56b03a06db2f85fd) )

	ROM_REGION( 0x10000, "cpu3", 0 )
	ROM_LOAD( "01d.bin",      0x0000, 0x0200, CRC(2747ca77) SHA1(abc0ca05925974c4b852827605ee2f1caefb8524) )

	ROM_REGION( 0x4000, "gfx8x8", 0 )
	ROM_LOAD( "03a_a5.bin",   0x0000, 0x4000, CRC(0636b8f4) SHA1(8b34773bf7b7071fb5c8ff443a08befae3b545ea) )

	ROM_REGION( 0x8000, "gfx16x16", 0 )
	ROM_LOAD( "04m_a6.bin",   0x0000, 0x2000, CRC(3bbc9b26) SHA1(1b1f79717e5c1a23eefb2d6df71ad95aac9f8be4) )
	ROM_LOAD( "04l_a7.bin",   0x2000, 0x2000, CRC(3dfaa9d1) SHA1(cc016019efe0a2bc38c66fadf792c05f6cabeeaa) )
	ROM_LOAD( "04j_a8.bin",   0x4000, 0x2000, CRC(9afb16e9) SHA1(f951e75af658623f3b0e18ff388990b2870fad53) )
	ROM_LOAD( "04h_a9.bin",   0x6000, 0x2000, CRC(af24bce0) SHA1(0f06c5d248c9c92f2a4636d259ab843339737969) )

	ROM_REGION( 0x0200, "proms", 0 )
	ROM_LOAD( "09c.bin",      0x0000, 0x0200, CRC(066f52bc) SHA1(99f4f2d0181bcaf389c16f127cc3e632d62ee417) ) // color prom
ROM_END

ROM_START( docastle2 )
	ROM_REGION( 0x10000, "cpu1", 0 )
	ROM_LOAD( "a1",           0x0000, 0x2000, CRC(0d81fafc) SHA1(938c4470b022063ae66b607ca086fad98174248f) )
	ROM_LOAD( "a2",           0x2000, 0x2000, CRC(a13dc4ac) SHA1(f096d5ac8e26444ebdb4c3fb5c71592058fb6c79) )
	ROM_LOAD( "a3",           0x4000, 0x2000, CRC(a1f04ffb) SHA1(8cb53992cc679278a0bd33e5b728f27c585e38e1) )
	ROM_LOAD( "a4",           0x6000, 0x2000, CRC(1fb14aa6) SHA1(45b50db61ca2c9ace182ddba3817257308f07c89) )

	ROM_REGION( 0x10000, "cpu2", 0 )
	ROM_LOAD( "a10",          0x0000, 0x4000, CRC(45f7f69b) SHA1(1c614c29be4950314ab04e0b828e691fd0907eff) )

	ROM_REGION( 0x10000, "cpu3", 0 )
	ROM_LOAD( "01d.bin",      0x0000, 0x0200, CRC(2747ca77) SHA1(abc0ca05925974c4b852827605ee2f1caefb8524) )

	ROM_REGION( 0x4000, "gfx8x8", 0 )
	ROM_LOAD( "03a_a5.bin",   0x0000, 0x4000, CRC(0636b8f4) SHA1(8b34773bf7b7071fb5c8ff443a08befae3b545ea) )

	ROM_REGION( 0x8000, "gfx16x16", 0 )
	ROM_LOAD( "04m_a6.bin",   0x0000, 0x2000, CRC(3bbc9b26) SHA1(1b1f79717e5c1a23eefb2d6df71ad95aac9f8be4) )
	ROM_LOAD( "04l_a7.bin",   0x2000, 0x2000, CRC(3dfaa9d1) SHA1(cc016019efe0a2bc38c66fadf792c05f6cabeeaa) )
	ROM_LOAD( "04j_a8.bin",   0x4000, 0x2000, CRC(9afb16e9) SHA1(f951e75af658623f3b0e18ff388990b2870fad53) )
	ROM_LOAD( "04h_a9.bin",   0x6000, 0x2000, CRC(af24bce0) SHA1(0f06c5d248c9c92f2a4636d259ab843339737969) )

	ROM_REGION( 0x0200, "proms", 0 )
	ROM_LOAD( "09c.bin",      0x0000, 0x0200, CRC(066f52bc) SHA1(99f4f2d0181bcaf389c16f127cc3e632d62ee417) ) // color prom
ROM_END

ROM_START( docastleo )
	ROM_REGION( 0x10000, "cpu1", 0 )
	ROM_LOAD( "c1.bin",     0x0000, 0x2000, CRC(c9ce96ab) SHA1(3166aef4556ce334ecef27dceb285f51de371c35) )
	ROM_LOAD( "c2.bin",     0x2000, 0x2000, CRC(42b28369) SHA1(8c9a618984db52cdc4ec3a6bcfa7659866d2709c) )
	ROM_LOAD( "c3.bin",     0x4000, 0x2000, CRC(c8c13124) SHA1(2cfc15b232744b40350c174b0d89677495c077eb) )
	ROM_LOAD( "c4.bin",     0x6000, 0x2000, CRC(7ca78471) SHA1(2804f9be825973e69bc35aa703145b3ef22a5ecd) )

	ROM_REGION( 0x10000, "cpu2", 0 )
	ROM_LOAD( "dorev10.bin",  0x0000, 0x4000, CRC(4b1925e3) SHA1(1b229dd73eede3853d23576dfe397a4d2d952991) )

	ROM_REGION( 0x10000, "cpu3", 0 )
	ROM_LOAD( "01d.bin",      0x0000, 0x0200, CRC(2747ca77) SHA1(abc0ca05925974c4b852827605ee2f1caefb8524) )

	ROM_REGION( 0x4000, "gfx8x8", 0 )
	ROM_LOAD( "03a_a5.bin",   0x0000, 0x4000, CRC(0636b8f4) SHA1(8b34773bf7b7071fb5c8ff443a08befae3b545ea) )

	ROM_REGION( 0x8000, "gfx16x16", 0 )
	ROM_LOAD( "dorev6.bin",   0x0000, 0x2000, CRC(9e335bf8) SHA1(30ccfb959c1d3069739280711ba3d7d8b7a1a2b0) )
	ROM_LOAD( "dorev7.bin",   0x2000, 0x2000, CRC(f5d5701d) SHA1(f2731aaa7be3e269cf929aa14db1c967c9198dc6) )
	ROM_LOAD( "dorev8.bin",   0x4000, 0x2000, CRC(7143ca68) SHA1(b5b45e3cf7d0287d93231eb01395a03f39f473fc) )
	ROM_LOAD( "dorev9.bin",   0x6000, 0x2000, CRC(893fc004) SHA1(15559d11cc14341d2fec190d12205a649bdd484f) )

	ROM_REGION( 0x0200, "proms", 0 )
	// which prom? this set has the same gfx as douni so i'm using that prom
//  ROM_LOAD( "09c.bin",      0x0000, 0x0200, CRC(066f52bc) SHA1(99f4f2d0181bcaf389c16f127cc3e632d62ee417) ) // color prom
	ROM_LOAD( "dorevc9.bin",  0x0000, 0x0200, CRC(96624ebe) SHA1(74ff21dc85dcb013c941ec6c06cafdb5bcc16960) ) // color prom
ROM_END

ROM_START( douni )
	ROM_REGION( 0x10000, "cpu1", 0 )
	ROM_LOAD( "dorev1.bin",   0x0000, 0x2000, CRC(1e2cbb3c) SHA1(f3c6eab7f7f43a067d432d47f3403ab1d07575fe) )
	ROM_LOAD( "dorev2.bin",   0x2000, 0x2000, CRC(18418f83) SHA1(1be77b0b53e6d243484d942108e84b950f1a4901) )
	ROM_LOAD( "dorev3.bin",   0x4000, 0x2000, CRC(7b9e2061) SHA1(2b21af54018a2ff756d80ba0b53b71108c0ce043) )
	ROM_LOAD( "dorev4.bin",   0x6000, 0x2000, CRC(e013954d) SHA1(1e05937f3b6eaef77c36b485da091ebb22e50f85) )

	ROM_REGION( 0x10000, "cpu2", 0 )
	ROM_LOAD( "dorev10.bin",  0x0000, 0x4000, CRC(4b1925e3) SHA1(1b229dd73eede3853d23576dfe397a4d2d952991) )

	ROM_REGION( 0x10000, "cpu3", 0 )
	ROM_LOAD( "01d.bin",      0x0000, 0x0200, CRC(2747ca77) SHA1(abc0ca05925974c4b852827605ee2f1caefb8524) )

	ROM_REGION( 0x4000, "gfx8x8", 0 )
	ROM_LOAD( "03a_a5.bin",   0x0000, 0x4000, CRC(0636b8f4) SHA1(8b34773bf7b7071fb5c8ff443a08befae3b545ea) )

	ROM_REGION( 0x8000, "gfx16x16", 0 )
	ROM_LOAD( "dorev6.bin",   0x0000, 0x2000, CRC(9e335bf8) SHA1(30ccfb959c1d3069739280711ba3d7d8b7a1a2b0) )
	ROM_LOAD( "dorev7.bin",   0x2000, 0x2000, CRC(f5d5701d) SHA1(f2731aaa7be3e269cf929aa14db1c967c9198dc6) )
	ROM_LOAD( "dorev8.bin",   0x4000, 0x2000, CRC(7143ca68) SHA1(b5b45e3cf7d0287d93231eb01395a03f39f473fc) )
	ROM_LOAD( "dorev9.bin",   0x6000, 0x2000, CRC(893fc004) SHA1(15559d11cc14341d2fec190d12205a649bdd484f) )

	ROM_REGION( 0x0200, "proms", 0 )
	ROM_LOAD( "dorevc9.bin",  0x0000, 0x0200, CRC(96624ebe) SHA1(74ff21dc85dcb013c941ec6c06cafdb5bcc16960) ) // color prom
ROM_END

ROM_START( dorunrunc )
	ROM_REGION( 0x10000, "cpu1", 0 )
	ROM_LOAD( "rev-0-1.p1",   0x0000, 0x2000, CRC(49906ebd) SHA1(03afb7e3107038d5a052e497b8a206334514536f) )
	ROM_LOAD( "rev-0-2.n1",   0x2000, 0x2000, CRC(dbe3e7db) SHA1(168026aacce73941329a72e78423f83f7c4f0f04) )
	ROM_LOAD( "rev-0-3.l1",   0x4000, 0x2000, CRC(e9b8181a) SHA1(6b960c3503589e62b61f9a2af372b98c48412bc0) )
	ROM_LOAD( "rev-0-4.k1",   0x6000, 0x2000, CRC(a63d0b89) SHA1(d2ab3b76149e6620f1eb93a051c802b208b8d6dc) )

	ROM_REGION( 0x10000, "cpu2", 0 )
	ROM_LOAD( "rev-0-2.n7",   0x0000, 0x4000, CRC(6dac2fa3) SHA1(cd583f379f01788ce20f611f17689105d32ef97a) )

	ROM_REGION( 0x10000, "cpu3", 0 )
	ROM_LOAD( "bprom2.bin",   0x0000, 0x0200, CRC(2747ca77) SHA1(abc0ca05925974c4b852827605ee2f1caefb8524) )

	ROM_REGION( 0x4000, "gfx8x8", 0 )
	ROM_LOAD( "rev-0-5.a3",   0x0000, 0x4000, CRC(e20795b7) SHA1(ae4366d2c45580f3e60ae36f81a5fc912d1eb899) )

	ROM_REGION( 0x8000, "gfx16x16", 0 )
	ROM_LOAD( "2764.m4",      0x0000, 0x2000, CRC(4bb231a0) SHA1(350423a1e602e23b229095021942d4b14a4736a7) )
	ROM_LOAD( "2764.l4",      0x2000, 0x2000, CRC(0c08508a) SHA1(1e235a0f44207c53af2c8da631e5a8e08b231258) )
	ROM_LOAD( "2764.j4",      0x4000, 0x2000, CRC(79287039) SHA1(e2e3c056f35a22e48115557e10fcd172ad2f91f1) )
	ROM_LOAD( "2764.h4",      0x6000, 0x2000, CRC(523aa999) SHA1(1d4aa0af79a2ed7b935d4ce92d978bf738f08eb3) )

	ROM_REGION( 0x0100, "proms", 0 )
	ROM_LOAD( "dorunrun.clr", 0x0000, 0x0100, CRC(d5bab5d5) SHA1(7a465fe30b6008793d33f6e07086c89111e1e407) )
ROM_END

ROM_START( dorunrunca )
	ROM_REGION( 0x10000, "cpu1", 0 )
	ROM_LOAD( "runrunc_p1",   0x0000, 0x2000, CRC(a8d789c6) SHA1(42eb5fb6a63300ea4ec71817b5cc9cbc7b0b4150) )
	ROM_LOAD( "runrunc_n1",   0x2000, 0x2000, CRC(f8c8a9f4) SHA1(0efd91758f951d8aadab4f5f0086413a8160d603) )
	ROM_LOAD( "runrunc_l1",   0x4000, 0x2000, CRC(223927ca) SHA1(8ae5587b4266d54ba7e8756ad07c867869ce3364) )
	ROM_LOAD( "runrunc_k1",   0x6000, 0x2000, CRC(5edd6752) SHA1(c7483755e2115420ab8a17287e4adfcd9bd1b5c4) )

	ROM_REGION( 0x10000, "cpu2", 0 )
	ROM_LOAD( "rev-0-2.n7",   0x0000, 0x4000, CRC(6dac2fa3) SHA1(cd583f379f01788ce20f611f17689105d32ef97a) )

	ROM_REGION( 0x10000, "cpu3", 0 )
	ROM_LOAD( "bprom2.bin",   0x0000, 0x0200, CRC(2747ca77) SHA1(abc0ca05925974c4b852827605ee2f1caefb8524) )

	ROM_REGION( 0x4000, "gfx8x8", 0 )
	ROM_LOAD( "rev-0-5.a3",   0x0000, 0x4000, CRC(e20795b7) SHA1(ae4366d2c45580f3e60ae36f81a5fc912d1eb899) )

	ROM_REGION( 0x8000, "gfx16x16", 0 )
	ROM_LOAD( "2764.m4",      0x0000, 0x2000, CRC(4bb231a0) SHA1(350423a1e602e23b229095021942d4b14a4736a7) )
	ROM_LOAD( "2764.l4",      0x2000, 0x2000, CRC(0c08508a) SHA1(1e235a0f44207c53af2c8da631e5a8e08b231258) )
	ROM_LOAD( "2764.j4",      0x4000, 0x2000, CRC(79287039) SHA1(e2e3c056f35a22e48115557e10fcd172ad2f91f1) )
	ROM_LOAD( "2764.h4",      0x6000, 0x2000, CRC(523aa999) SHA1(1d4aa0af79a2ed7b935d4ce92d978bf738f08eb3) )

	ROM_REGION( 0x0100, "proms", 0 )
	ROM_LOAD( "dorunrun.clr", 0x0000, 0x0100, CRC(d5bab5d5) SHA1(7a465fe30b6008793d33f6e07086c89111e1e407) )
ROM_END

 // Italian bootleg of dorunrunc. Main PCB + 040285C riser board. Differences are just revised number of lives and the 'Do!' and copyright drawing routines NOPed out.
ROM_START( runrun )
	ROM_REGION( 0x10000, "cpu1", 0 )
	ROM_LOAD( "electric_1.bin",   0x0000, 0x2000, CRC(9f23896b) SHA1(609103729b0d9721166ee5e50e4adab09d343f0a) ) // only different ROM
	ROM_LOAD( "electric_2.bin",   0x2000, 0x2000, CRC(dbe3e7db) SHA1(168026aacce73941329a72e78423f83f7c4f0f04) )
	ROM_LOAD( "electric_3.bin",   0x4000, 0x2000, CRC(e9b8181a) SHA1(6b960c3503589e62b61f9a2af372b98c48412bc0) )
	ROM_LOAD( "electric_4.bin",   0x6000, 0x2000, CRC(a63d0b89) SHA1(d2ab3b76149e6620f1eb93a051c802b208b8d6dc) )

	ROM_REGION( 0x10000, "cpu2", 0 )
	ROM_LOAD( "electric_10.bin",   0x0000, 0x4000, CRC(6dac2fa3) SHA1(cd583f379f01788ce20f611f17689105d32ef97a) )

	ROM_REGION( 0x10000, "cpu3", 0 )
	ROM_LOAD( "bprom2.bin",   0x0000, 0x0200, BAD_DUMP CRC(2747ca77) SHA1(abc0ca05925974c4b852827605ee2f1caefb8524) ) // not dumped for this set

	ROM_REGION( 0x4000, "gfx8x8", 0 )
	ROM_LOAD( "electric_5.bin",   0x0000, 0x4000, CRC(e20795b7) SHA1(ae4366d2c45580f3e60ae36f81a5fc912d1eb899) )

	ROM_REGION( 0x8000, "gfx16x16", 0 )
	ROM_LOAD( "electric_6.bin",      0x0000, 0x2000, CRC(4bb231a0) SHA1(350423a1e602e23b229095021942d4b14a4736a7) )
	ROM_LOAD( "electric_7.bin",      0x2000, 0x2000, CRC(0c08508a) SHA1(1e235a0f44207c53af2c8da631e5a8e08b231258) )
	ROM_LOAD( "electric_8.bin",      0x4000, 0x2000, CRC(79287039) SHA1(e2e3c056f35a22e48115557e10fcd172ad2f91f1) )
	ROM_LOAD( "electric_9.bin",      0x6000, 0x2000, CRC(523aa999) SHA1(1d4aa0af79a2ed7b935d4ce92d978bf738f08eb3) )

	ROM_REGION( 0x0100, "proms", 0 )
	ROM_LOAD( "dorunrun.clr", 0x0000, 0x0100, BAD_DUMP CRC(d5bab5d5) SHA1(7a465fe30b6008793d33f6e07086c89111e1e407) ) // not dumped for this set
ROM_END

ROM_START( dorunrun )
	ROM_REGION( 0x10000, "cpu1", 0 )
	ROM_LOAD( "2764.p1",      0x0000, 0x2000, CRC(95c86f8e) SHA1(9fe44911e0aa8d4c7299472a31c401e064d63d17) )
	ROM_LOAD( "2764.l1",      0x4000, 0x2000, CRC(e9a65ba7) SHA1(fbee57d68352fd4062aac55cd1070f001714d0a3) )
	ROM_LOAD( "2764.k1",      0x6000, 0x2000, CRC(b1195d3d) SHA1(095ad2ee1f53be3203830263cb0c9efbe4710c56) )
	ROM_LOAD( "2764.n1",      0x8000, 0x2000, CRC(6a8160d1) SHA1(12101c351bf800319172c459b5e7c69cb5603806) )

	ROM_REGION( 0x10000, "cpu2", 0 )
	ROM_LOAD( "27128.p7",     0x0000, 0x4000, CRC(8b06d461) SHA1(2434478810c6301197997be76505f5fc6beba5d3) )

	ROM_REGION( 0x10000, "cpu3", 0 )
	ROM_LOAD( "bprom2.bin",   0x0000, 0x0200, CRC(2747ca77) SHA1(abc0ca05925974c4b852827605ee2f1caefb8524) )

	ROM_REGION( 0x4000, "gfx8x8", 0 )
	ROM_LOAD( "27128.a3",     0x0000, 0x4000, CRC(4be96dcf) SHA1(f9b45e6297cbbc4d1ee2df7ac377c5daf5181b0f) )

	ROM_REGION( 0x8000, "gfx16x16", 0 )
	ROM_LOAD( "2764.m4",      0x0000, 0x2000, CRC(4bb231a0) SHA1(350423a1e602e23b229095021942d4b14a4736a7) )
	ROM_LOAD( "2764.l4",      0x2000, 0x2000, CRC(0c08508a) SHA1(1e235a0f44207c53af2c8da631e5a8e08b231258) )
	ROM_LOAD( "2764.j4",      0x4000, 0x2000, CRC(79287039) SHA1(e2e3c056f35a22e48115557e10fcd172ad2f91f1) )
	ROM_LOAD( "2764.h4",      0x6000, 0x2000, CRC(523aa999) SHA1(1d4aa0af79a2ed7b935d4ce92d978bf738f08eb3) )

	ROM_REGION( 0x0100, "proms", 0 )
	ROM_LOAD( "dorunrun.clr", 0x0000, 0x0100, CRC(d5bab5d5) SHA1(7a465fe30b6008793d33f6e07086c89111e1e407) )
ROM_END

ROM_START( dorunrun2 )
	ROM_REGION( 0x10000, "cpu1", 0 )
	ROM_LOAD( "p1",           0x0000, 0x2000, CRC(12a99365) SHA1(12a1ab76182faa4f76cc5020913ca5706313fe72) )
	ROM_LOAD( "l1",           0x4000, 0x2000, CRC(38609287) SHA1(85f5cd707d620780436e4bed00753acef08f83cd) )
	ROM_LOAD( "k1",           0x6000, 0x2000, CRC(099aaf54) SHA1(c0419db2a2349ecb97c31256811993d1dcf3dc6e) )
	ROM_LOAD( "n1",           0x8000, 0x2000, CRC(4f8fcbae) SHA1(c1558664e081252141530e1932403df1fbf5f8a0) )

	ROM_REGION( 0x10000, "cpu2", 0 )
	ROM_LOAD( "27128.p7",     0x0000, 0x4000, CRC(8b06d461) SHA1(2434478810c6301197997be76505f5fc6beba5d3) )

	ROM_REGION( 0x10000, "cpu3", 0 )
	ROM_LOAD( "bprom2.bin",   0x0000, 0x0200, CRC(2747ca77) SHA1(abc0ca05925974c4b852827605ee2f1caefb8524) )

	ROM_REGION( 0x4000, "gfx8x8", 0 )
	ROM_LOAD( "27128.a3",     0x0000, 0x4000, CRC(4be96dcf) SHA1(f9b45e6297cbbc4d1ee2df7ac377c5daf5181b0f) )

	ROM_REGION( 0x8000, "gfx16x16", 0 )
	ROM_LOAD( "2764.m4",      0x0000, 0x2000, CRC(4bb231a0) SHA1(350423a1e602e23b229095021942d4b14a4736a7) )
	ROM_LOAD( "2764.l4",      0x2000, 0x2000, CRC(0c08508a) SHA1(1e235a0f44207c53af2c8da631e5a8e08b231258) )
	ROM_LOAD( "2764.j4",      0x4000, 0x2000, CRC(79287039) SHA1(e2e3c056f35a22e48115557e10fcd172ad2f91f1) )
	ROM_LOAD( "2764.h4",      0x6000, 0x2000, CRC(523aa999) SHA1(1d4aa0af79a2ed7b935d4ce92d978bf738f08eb3) )

	ROM_REGION( 0x0100, "proms", 0 )
	ROM_LOAD( "dorunrun.clr", 0x0000, 0x0100, CRC(d5bab5d5) SHA1(7a465fe30b6008793d33f6e07086c89111e1e407) )
ROM_END

ROM_START( spiero )
	ROM_REGION( 0x10000, "cpu1", 0 )
	ROM_LOAD( "sp1.bin",      0x0000, 0x2000, CRC(08d23e38) SHA1(0810b0ecaa1bd7f16f78ce08054f5d24a57b1266) )
	ROM_LOAD( "sp3.bin",      0x4000, 0x2000, CRC(faa0c18c) SHA1(0dadea03b529bb889f45bd710bca0b4333cbbba8) )
	ROM_LOAD( "sp4.bin",      0x6000, 0x2000, CRC(639b4e5d) SHA1(cf252869fa0c5351f093026996c4e372f19a52a9) )
	ROM_LOAD( "sp2.bin",      0x8000, 0x2000, CRC(3a29ccb0) SHA1(37d6ce598a9c3b5dbb23c19a4bd94265287f83f7) )

	ROM_REGION( 0x10000, "cpu2", 0 )
	ROM_LOAD( "27128.p7",     0x0000, 0x4000, CRC(8b06d461) SHA1(2434478810c6301197997be76505f5fc6beba5d3) )

	ROM_REGION( 0x10000, "cpu3", 0 )
	ROM_LOAD( "bprom2.bin",   0x0000, 0x0200, CRC(2747ca77) SHA1(abc0ca05925974c4b852827605ee2f1caefb8524) )

	ROM_REGION( 0x4000, "gfx8x8", 0 )
	ROM_LOAD( "sp5.bin",      0x0000, 0x4000, CRC(1b704bb0) SHA1(db3b2f120d632b5f897c47aee115916ec6c52a69) )

	ROM_REGION( 0x8000, "gfx16x16", 0 )
	ROM_LOAD( "sp6.bin",      0x0000, 0x2000, CRC(00f893a7) SHA1(9680d59065a18ea25734bdcd9a1dd1d5d721a47b) )
	ROM_LOAD( "sp7.bin",      0x2000, 0x2000, CRC(173e5c6a) SHA1(5d06b8702e90af122c347e20b01811994165e727) )
	ROM_LOAD( "sp8.bin",      0x4000, 0x2000, CRC(2e66525a) SHA1(50e7b5e5f01d961eb311c65321fc536d8e4eb7b0) )
	ROM_LOAD( "sp9.bin",      0x6000, 0x2000, CRC(9c571525) SHA1(c7f1c22c6decd6326ef188bbf440115c1e2b16f4) )

	ROM_REGION( 0x0200, "proms", 0 )
	ROM_LOAD( "bprom1.bin",   0x0000, 0x0200, CRC(fc1b66ff) SHA1(0a73f7e00501c638f017473b1e0786d7bcbbe82a) ) // color prom
ROM_END

ROM_START( dowild ) // UNIVERSAL 8339A PCB
	ROM_REGION( 0x10000, "cpu1", 0 )
	ROM_LOAD( "w1",           0x0000, 0x2000, CRC(097de78b) SHA1(8d0cedde09a893ff67db0cb8e239babeb2cb3701) )
	ROM_LOAD( "w3",           0x4000, 0x2000, CRC(fc6a1cbb) SHA1(4cf59459d521c725e41bbd9363fb58bffdad13a2) )
	ROM_LOAD( "w4",           0x6000, 0x2000, CRC(8aac1d30) SHA1(c746f5506a541b25a7ca6fc754fbdb94212f7178) )
	ROM_LOAD( "w2",           0x8000, 0x2000, CRC(0914ab69) SHA1(048a68976d313015ff40f411d5c89d318fd9bb04) )

	ROM_REGION( 0x10000, "cpu2", 0 )
	ROM_LOAD( "w10",          0x0000, 0x4000, CRC(d1f37fba) SHA1(827e2e3b140c4df2fd8780d7d05dc45694cf8f02) )

	ROM_REGION( 0x10000, "cpu3", 0 )
	ROM_LOAD( "8300b-2",      0x0000, 0x0200, CRC(2747ca77) SHA1(abc0ca05925974c4b852827605ee2f1caefb8524) )

	ROM_REGION( 0x4000, "gfx8x8", 0 )
	ROM_LOAD( "w5",           0x0000, 0x4000, CRC(b294b151) SHA1(c8c15bf9ab2401052ec80fdfc8fe124c6aa52521) )

	ROM_REGION( 0x8000, "gfx16x16", 0 )
	ROM_LOAD( "w6",           0x0000, 0x2000, CRC(57e0208b) SHA1(52c1157d7df84fef754cd9466a70df7e53bc837f) )
	ROM_LOAD( "w7",           0x2000, 0x2000, CRC(5001a6f7) SHA1(ef37a26f0c2960fc55ff800da5d4d9c3d54270c2) )
	ROM_LOAD( "w8",           0x4000, 0x2000, CRC(ec503251) SHA1(ea7bb11c8e51fe69b5ecba4e4806ec8a8e4961d7) )
	ROM_LOAD( "w9",           0x6000, 0x2000, CRC(af7bd7eb) SHA1(1cc017b4606d1f6d70cc6cdaf9cf7797dd552b4b) )

	ROM_REGION( 0x0100, "proms", 0 )
	ROM_LOAD( "dowild.clr",   0x0000, 0x0100, CRC(a703dea5) SHA1(159abdb62cb8bf3167d9fdc26038fb485219af7c) )
ROM_END

ROM_START( jjack )
	ROM_REGION( 0x10000, "cpu1", 0 )
	ROM_LOAD( "j1.bin",       0x0000, 0x2000, CRC(87f29bd2) SHA1(12b0a6f84439b84dd09fe54436c765fc58f928e1) )
	ROM_LOAD( "j3.bin",       0x4000, 0x2000, CRC(35b0517e) SHA1(756d7627f4dc2e9b24be7b0c4c80a9043cb4c322) )
	ROM_LOAD( "j4.bin",       0x6000, 0x2000, CRC(35bb316a) SHA1(8461503179eb7798de6f9f6677eb18ebcd22d470) )
	ROM_LOAD( "j2.bin",       0x8000, 0x2000, CRC(dec52e80) SHA1(1620a070a75115d7c35f44574a8b53ce137ce21a) )

	ROM_REGION( 0x10000, "cpu2", 0 )
	ROM_LOAD( "j0.bin",       0x0000, 0x4000, CRC(ab042f04) SHA1(06412dbdc43ebd6d376a811f240a4c9ec43ca6e7) )

	ROM_REGION( 0x10000, "cpu3", 0 )
	ROM_LOAD( "bprom2.bin",   0x0000, 0x0200, CRC(2747ca77) SHA1(abc0ca05925974c4b852827605ee2f1caefb8524) )

	ROM_REGION( 0x4000, "gfx8x8", 0 )
	ROM_LOAD( "j5.bin",       0x0000, 0x4000, CRC(75038ff9) SHA1(2a92e0d0adb1abd029c5ee6e350a859fed3f0ae9) )

	ROM_REGION( 0x8000, "gfx16x16", 0 )
	ROM_LOAD( "j6.bin",       0x0000, 0x2000, CRC(5937bd7b) SHA1(4d399370422e22448d1680113d5bbe9894086a2b) )
	ROM_LOAD( "j7.bin",       0x2000, 0x2000, CRC(cf8ae8e7) SHA1(81b881eda0bf60f52dadf22aafc9ca7162fd1540) )
	ROM_LOAD( "j8.bin",       0x4000, 0x2000, CRC(84f6fc8c) SHA1(be631af9525262cf98c982b88cb97d3287366731) )
	ROM_LOAD( "j9.bin",       0x6000, 0x2000, CRC(3f9bb09f) SHA1(a38f7c9a21e37c7b903497e8170f64a378df730e) )

	ROM_REGION( 0x0200, "proms", 0 )
	ROM_LOAD( "bprom1.bin",   0x0000, 0x0200, CRC(2f0955f2) SHA1(5eb417478669560f447a0a0e6fe93af27804590f) ) // color prom
ROM_END

ROM_START( kickridr )
	ROM_REGION( 0x10000, "cpu1", 0 )
	ROM_LOAD( "k1",           0x0000, 0x2000, CRC(dfdd1ab4) SHA1(2f1ad3a8b2c1cbca2dc4a9a0c9a5f79af8712999) )
	ROM_LOAD( "k3",           0x4000, 0x2000, CRC(412244da) SHA1(2d4efad88c27db00c18626a667bd00531f4cc4fb) )
	ROM_LOAD( "k4",           0x6000, 0x2000, CRC(a67dd2ec) SHA1(d35eefd314c7a7d9badffc8a19270380f01e263c) )
	ROM_LOAD( "k2",           0x8000, 0x2000, CRC(e193fb5c) SHA1(3719c012bb1d75e79aa111093864b6e6bb46bc8c) )

	ROM_REGION( 0x10000, "cpu2", 0 )
	ROM_LOAD( "k10",          0x0000, 0x4000, CRC(6843dbc0) SHA1(8a83f785e1fc2fa34a9955c19e27a1968a6d3f08) )

	ROM_REGION( 0x10000, "cpu3", 0 )
	ROM_LOAD( "8300b-2",      0x0000, 0x0200, CRC(2747ca77) SHA1(abc0ca05925974c4b852827605ee2f1caefb8524) )

	ROM_REGION( 0x4000, "gfx8x8", 0 )
	ROM_LOAD( "k5",           0x0000, 0x4000, CRC(3f7d7e49) SHA1(ed9697656a46c3e036665659a008396b323c9c2b) )

	ROM_REGION( 0x8000, "gfx16x16", 0 )
	ROM_LOAD( "k6",           0x0000, 0x2000, CRC(94252ed3) SHA1(ec7a31d786104a8ebff9229d89e78a074b1b5205) )
	ROM_LOAD( "k7",           0x2000, 0x2000, CRC(7ef2420e) SHA1(b04443ba9f7d19b0b8c15fab2aa7d86febb2add3) )
	ROM_LOAD( "k8",           0x4000, 0x2000, CRC(29bed201) SHA1(f44b2a65d3bd6612a726dd17c3e5e29e7f8196b2) )
	ROM_LOAD( "k9",           0x6000, 0x2000, CRC(847584d3) SHA1(14aa799a2d060f49fc955c760b0e97e9c1ac1662) )

	ROM_REGION( 0x0100, "proms", 0 )
	ROM_LOAD( "kickridr.clr", 0x0000, 0x0100, CRC(73ec281c) SHA1(1af32653e03c1e0aadef47b91bdc3f3c56ef7b23) )
ROM_END

ROM_START( idsoccer )
	ROM_REGION( 0x10000, "cpu1", 0 )
	ROM_LOAD( "id01",           0x0000, 0x2000, CRC(f1c3bf09) SHA1(446d373671f122d8131d2ec2d80c2110ec68a19b) )
	ROM_LOAD( "id02",           0x2000, 0x2000, CRC(184e6af0) SHA1(1664ca6fa0efae8496d051ac5f19596239e7cbcb) )
	ROM_LOAD( "id03",           0x6000, 0x2000, CRC(22524661) SHA1(363528a1135d11ead03c76064042a72e0ac93533) )
	ROM_LOAD( "id04",           0x8000, 0x2000, CRC(e8cd95fd) SHA1(2e272c154bd5dd2dca58d3fe12c6ba5e01a62477) )

	ROM_REGION( 0x10000, "cpu2", 0 )
	ROM_LOAD( "id10",           0x0000, 0x4000, CRC(6c8b2037) SHA1(718d680186623d5af23ed272f04e726fbb17f078) )

	ROM_REGION( 0x10000, "cpu3", 0 )
	ROM_LOAD( "id_8p",          0x0000, 0x0200, CRC(2747ca77) SHA1(abc0ca05925974c4b852827605ee2f1caefb8524) )

	ROM_REGION( 0x8000, "gfx8x8", 0 )
	ROM_LOAD( "id05",          0x0000, 0x4000, CRC(a57c7a11) SHA1(9faebad0050da05101811427f350e163a7811396) )

	ROM_REGION( 0x20000, "gfx16x16", 0 )
	ROM_LOAD( "id06",           0x00000, 0x8000, CRC(b42a6f4a) SHA1(ddce4438b3649610bd3703cbd7592aaa9a3eda0e) )
	ROM_LOAD( "id07",           0x08000, 0x8000, CRC(fa2b1c77) SHA1(0d8e9db065c76621deb58575f01c6ec5ee6cf6b0) )
	ROM_LOAD( "id08",           0x10000, 0x8000, CRC(5e97eab9) SHA1(40d261a0255c594353816c18aa6c0c245aeb68a8) )
	ROM_LOAD( "id09",           0x18000, 0x8000, CRC(a2a69223) SHA1(6bd9b76e0119643450c9f64c80b52e9056da82d6) )

	ROM_REGION( 0x10000, "adpcm", 0 )
	ROM_LOAD( "is1",            0x0000, 0x4000, CRC(9eb76196) SHA1(c15331dd8c3efaa83a95245210d05eaaa64b3161) )
	ROM_LOAD( "is3",            0x8000, 0x4000, CRC(27bebba3) SHA1(cf752b22603c1e2a0b33958481c652d6d56ebf68) )
	ROM_LOAD( "is4",            0xc000, 0x4000, CRC(dd5ffaa2) SHA1(4bc4330a54ca93448a8fe05207d3fb1a3a9872e1) )

	ROM_REGION( 0x0200, "proms", 0 )
	ROM_LOAD( "id_3d.clr",   0x0000, 0x0200, CRC(a433ff62) SHA1(db9afe5fc917d25aafa21576cb1cecec7481d4cb) )
ROM_END

ROM_START( idsoccera )
	ROM_REGION( 0x10000, "cpu1", 0 )
	ROM_LOAD( "indoor.e10", 0x00000, 0x2000, CRC(ed086bd1) SHA1(e1351fe53779c73a8631d4c33fa9508ddac8b4c1) )
	ROM_LOAD( "indoor.f10", 0x02000, 0x2000, CRC(54e5c6fc) SHA1(2c5a1e7bb9c8d5877eb99e8d20498986b85a8af4) )
	ROM_LOAD( "indoor.h10", 0x06000, 0x2000, CRC(f0ca1ac8) SHA1(fa5724ba8ab0ff82a1ef22ddd56e57b4c2b6ab74) )

	ROM_REGION( 0x10000, "cpu2", 0 )
	ROM_LOAD( "indoor.e2",  0x00000, 0x4000, CRC(c4bacc14) SHA1(d457a24b084726fe6b2f97a1be44e67c0a61a97b) ) // different

	ROM_REGION( 0x10000, "cpu3", 0 )
	ROM_LOAD( "indoor.p8",  0x00000, 0x0200, CRC(2747ca77) SHA1(abc0ca05925974c4b852827605ee2f1caefb8524) )

	ROM_REGION( 0x8000, "gfx8x8", 0 )
	ROM_LOAD( "indoor.e6",  0x00000, 0x4000, CRC(a57c7a11) SHA1(9faebad0050da05101811427f350e163a7811396) )

	// some graphic / sound ROMs also differ on this set - verify them
	// the sound ROM causes bad sound, but the implementation is hacked
	ROM_REGION( 0x20000, "gfx16x16", 0 )
	ROM_LOAD( "indoor.p3",  0x00000, 0x8000, CRC(b42a6f4a) SHA1(ddce4438b3649610bd3703cbd7592aaa9a3eda0e) )
	ROM_LOAD( "indoor.n3",  0x08000, 0x8000, CRC(fa2b1c77) SHA1(0d8e9db065c76621deb58575f01c6ec5ee6cf6b0) )
	ROM_LOAD( "indoor.l3",  0x10000, 0x8000, CRC(2663405c) SHA1(16c054c5c16ace80941523a64654afa3a77d7611) ) // different
	ROM_LOAD( "indoor.k3",  0x18000, 0x8000, CRC(a2a69223) SHA1(6bd9b76e0119643450c9f64c80b52e9056da82d6) )

	ROM_REGION( 0x10000, "adpcm", 0 )
	ROM_LOAD( "indoor.ic1", 0x00000, 0x4000, CRC(3bb65dc7) SHA1(499151903b3da9fa2455b3d2c04863b3e33e853d) ) // different (causes bad sound in attract, but isn't a bad dump since it has been confirmed on multiple boards)
	ROM_LOAD( "indoor.ic3", 0x08000, 0x4000, CRC(27bebba3) SHA1(cf752b22603c1e2a0b33958481c652d6d56ebf68) )
	ROM_LOAD( "indoor.ic4", 0x0c000, 0x4000, CRC(dd5ffaa2) SHA1(4bc4330a54ca93448a8fe05207d3fb1a3a9872e1) )

	ROM_REGION( 0x200, "proms", 0 )
	ROM_LOAD( "indoor.d3",  0x00000, 0x0200, CRC(d9b2550c) SHA1(074253b1ede42a743f1a8858756640693126209f) ) // different
ROM_END

ROM_START( idsoccert ) // UNIVERSAL 8461-A (with TECFRI SA logo) + UNIVERSAL 8461-SUB-1 PCBs. Uses a SY6845BA with a 10MHz XTAL instead.
	ROM_REGION( 0x10000, "cpu1", 0 )
	ROM_LOAD( "1.e10",        0x0000, 0x2000, CRC(1fa5ad7d) SHA1(f9d8dd0c8300085e84472014f8573cad637dbea2) )
	ROM_LOAD( "2.f10",        0x2000, 0x2000, CRC(68b9764b) SHA1(73811d71aef4c43ea99a8a578b38e7836d377536) )
	ROM_LOAD( "3.h10",        0x6000, 0x2000, CRC(5baabe1f) SHA1(12afaeac45ce03499614708edd171f20f3b4a73d) )

	ROM_REGION( 0x10000, "cpu2", 0 )
	ROM_LOAD( "9.e2",         0x0000, 0x4000, CRC(c4bacc14) SHA1(d457a24b084726fe6b2f97a1be44e67c0a61a97b) )

	ROM_REGION( 0x10000, "cpu3", 0 )
	ROM_LOAD( "82s147.p8",    0x0000, 0x0200, CRC(2747ca77) SHA1(abc0ca05925974c4b852827605ee2f1caefb8524) )

	ROM_REGION( 0x8000, "gfx8x8", 0 )
	ROM_LOAD( "4.e6",         0x0000, 0x4000, CRC(a57c7a11) SHA1(9faebad0050da05101811427f350e163a7811396) )

	ROM_REGION( 0x20000, "gfx16x16", 0 )
	ROM_LOAD( "8.p3",         0x00000, 0x8000, CRC(b42a6f4a) SHA1(ddce4438b3649610bd3703cbd7592aaa9a3eda0e) )
	ROM_LOAD( "7.n3",         0x08000, 0x8000, CRC(fa2b1c77) SHA1(0d8e9db065c76621deb58575f01c6ec5ee6cf6b0) )
	ROM_LOAD( "6.l3",         0x10000, 0x8000, CRC(2663405c) SHA1(16c054c5c16ace80941523a64654afa3a77d7611) )
	ROM_LOAD( "5.k3",         0x18000, 0x8000, CRC(a2a69223) SHA1(6bd9b76e0119643450c9f64c80b52e9056da82d6) )

	ROM_REGION( 0x10000, "adpcm", 0 )
	ROM_LOAD( "10_sub-1.ic1", 0x0000, 0x4000, CRC(3bb65dc7) SHA1(499151903b3da9fa2455b3d2c04863b3e33e853d) )
	// ic2 empty
	ROM_LOAD( "11_sub-1.ic3", 0x8000, 0x4000, CRC(27bebba3) SHA1(cf752b22603c1e2a0b33958481c652d6d56ebf68) )
	ROM_LOAD( "12_sub-1.ic4", 0xc000, 0x4000, CRC(3bb65dc7) SHA1(499151903b3da9fa2455b3d2c04863b3e33e853d) ) // same as ic1, verified. Really strange. Manufacturing error?

	ROM_REGION( 0x0200, "proms", 0 )
	ROM_LOAD( "4_82s147.d3", 0x0000, 0x0200, CRC(d9b2550c) SHA1(074253b1ede42a743f1a8858756640693126209f) )
ROM_END

/*
    American Soccer

    Main Board:    8461-A
    Samples Board: 8461-SUB
*/

ROM_START( asoccer )
	ROM_REGION( 0x10000, "cpu1", 0 ) // These roms are located on the 8461-A board.
	ROM_LOAD( "as1.e10",   0x00000, 0x2000, CRC(e9d61bbf) SHA1(8f9b3a6fd99f136698035263a20f39c3174b70cf) )
	ROM_LOAD( "as2.f10",   0x02000, 0x2000, CRC(62b2e4c8) SHA1(4270148652b18006c7df1f612f9b19f06e5400de) )
	ROM_LOAD( "as3.h10",   0x06000, 0x2000, CRC(25bbd0d8) SHA1(fa7fb4b78e5ac4200ff5f57f94794632af450ce0) )
	ROM_LOAD( "as4.k10",   0x08000, 0x2000, CRC(8d8bdc08) SHA1(75da310576047102af45c3a5f7d20893ef260d40) )

	ROM_REGION( 0x10000, "cpu2", 0 ) // These roms are located on the 8461-A board.
	ROM_LOAD( "as0.e2",    0x00000, 0x4000, CRC(05d613bf) SHA1(ab822fc532fc7f1122b5ff0385b268513e7e193e) )

	ROM_REGION( 0x10000, "cpu3", 0 ) // These roms are located on the 8461-A board. */
	ROM_LOAD( "200b.p8",   0x00000, 0x0200, CRC(2747ca77) SHA1(abc0ca05925974c4b852827605ee2f1caefb8524) ) /* 82S147 PROM */

	ROM_REGION( 0x8000, "gfx8x8", 0 ) /* These roms are located on the 8461-A board. */
	ROM_LOAD( "as5-2.e6",  0x00000, 0x4000, CRC(430295c4) SHA1(b1f4d9ab3ec0c969da4db51f476c47e87d48b879) )

	ROM_REGION( 0x20000, "gfx16x16", 0 ) /* These roms are located on the 8461-A board. */
	ROM_LOAD( "as6.p3-2",  0x00000, 0x8000, CRC(ae577023) SHA1(61e8f441eca1ff64760bae8139c8fa378ff5fbd2) )
	ROM_LOAD( "as7.n3-2",  0x08000, 0x8000, CRC(a20ddd9b) SHA1(530e5371b7f52031e555c98d1ff70c7beba94d4a) )
	ROM_LOAD( "as8.l3-2",  0x10000, 0x8000, CRC(dafad065) SHA1(a491680b642bd1ea4936f914297e61d4e3ccad88) )
	ROM_LOAD( "as9.j3-2",  0x18000, 0x8000, CRC(3a2ae776) SHA1(d9682abd30f64c51498c678257797d125b1c6a43) )

	ROM_REGION( 0x10000, "adpcm", 0 ) // These roms are located on the 8461-SUB board. */
	ROM_LOAD( "1.ic1",     0x00000, 0x4000, CRC(3bb65dc7) SHA1(499151903b3da9fa2455b3d2c04863b3e33e853d) )
	// Verified on board that IC2 is not populated.
	ROM_LOAD( "3.ic3",     0x08000, 0x4000, CRC(27bebba3) SHA1(cf752b22603c1e2a0b33958481c652d6d56ebf68) )
	ROM_LOAD( "4.ic4",     0x0c000, 0x4000, CRC(dd5ffaa2) SHA1(4bc4330a54ca93448a8fe05207d3fb1a3a9872e1) )

	ROM_REGION( 0x0200, "proms", 0 ) // These roms are located on the 8461-A board.
	ROM_LOAD( "3-2d.d3-2", 0x00000, 0x0200, CRC(a433ff62) SHA1(db9afe5fc917d25aafa21576cb1cecec7481d4cb) ) /* 82S147 PROM */
ROM_END

} // anonymous namespace

/* Game Drivers */

GAME( 1983, docastle,   0,        docastle, docastle, docastle_state, empty_init, ROT270, "Universal", "Mr. Do's Castle (set 1)", MACHINE_SUPPORTS_SAVE )
GAME( 1983, docastle2,  docastle, docastle, docastle, docastle_state, empty_init, ROT270, "Universal", "Mr. Do's Castle (set 2)", MACHINE_SUPPORTS_SAVE )
GAME( 1983, docastleo,  docastle, docastle, docastle, docastle_state, empty_init, ROT270, "Universal", "Mr. Do's Castle (older)", MACHINE_SUPPORTS_SAVE )
GAME( 1983, douni,      docastle, docastle, docastle, docastle_state, empty_init, ROT270, "Universal", "Mr. Do vs. Unicorns", MACHINE_SUPPORTS_SAVE )

GAME( 1984, dorunrun,   0,        dorunrun, dorunrun, docastle_state, empty_init, ROT0,   "Universal", "Do! Run Run (set 1)", MACHINE_SUPPORTS_SAVE )
GAME( 1984, dorunrun2,  dorunrun, dorunrun, dorunrun, docastle_state, empty_init, ROT0,   "Universal", "Do! Run Run (set 2)", MACHINE_SUPPORTS_SAVE )
GAME( 1984, dorunrunc,  dorunrun, docastle, dorunrun, docastle_state, empty_init, ROT0,   "Universal", "Do! Run Run (Do's Castle hardware, set 1)", MACHINE_SUPPORTS_SAVE )
GAME( 1984, dorunrunca, dorunrun, docastle, dorunrun, docastle_state, empty_init, ROT0,   "Universal", "Do! Run Run (Do's Castle hardware, set 2)", MACHINE_SUPPORTS_SAVE )
GAME( 1984, runrun,     dorunrun, docastle, runrun,   docastle_state, empty_init, ROT0,   "bootleg",   "Run Run (Do! Run Run bootleg)", MACHINE_SUPPORTS_SAVE )
GAME( 1987, spiero,     dorunrun, dorunrun, dorunrun, docastle_state, empty_init, ROT0,   "Universal", "Super Pierrot (Japan)", MACHINE_SUPPORTS_SAVE )

GAME( 1984, dowild,     0,        dorunrun, dowild,   docastle_state, empty_init, ROT0,   "Universal", "Mr. Do's Wild Ride", MACHINE_SUPPORTS_SAVE )

GAME( 1984, jjack,      0,        dorunrun, jjack,    docastle_state, empty_init, ROT270, "Universal", "Jumping Jack", MACHINE_SUPPORTS_SAVE )

GAME( 1984, kickridr,   0,        dorunrun, kickridr, docastle_state, empty_init, ROT0,   "Universal", "Kick Rider", MACHINE_SUPPORTS_SAVE )

GAME( 1985, idsoccer,   0,        idsoccer, idsoccer, idsoccer_state, empty_init, ROT0,   "Universal", "Indoor Soccer (set 1)", MACHINE_SUPPORTS_SAVE | MACHINE_NO_COCKTAIL )
GAME( 1985, idsoccera,  idsoccer, idsoccer, idsoccer, idsoccer_state, empty_init, ROT0,   "Universal", "Indoor Soccer (set 2)", MACHINE_SUPPORTS_SAVE | MACHINE_NO_COCKTAIL | MACHINE_IMPERFECT_SOUND | MACHINE_NOT_WORKING ) // see MT05419
GAME( 1985, idsoccert,  idsoccer, idsoccer, idsoccer, idsoccer_state, empty_init, ROT0,   "Universal (Tecfri license)", "Indoor Soccer (Tecfri license PCB)", MACHINE_SUPPORTS_SAVE | MACHINE_NO_COCKTAIL | MACHINE_IMPERFECT_SOUND | MACHINE_NOT_WORKING ) // see MT05419
GAME( 1987, asoccer,    idsoccer, idsoccer, idsoccer, idsoccer_state, empty_init, ROT0,   "Universal", "American Soccer", MACHINE_SUPPORTS_SAVE | MACHINE_NO_COCKTAIL | MACHINE_IMPERFECT_SOUND | MACHINE_NOT_WORKING ) // see MT05419
