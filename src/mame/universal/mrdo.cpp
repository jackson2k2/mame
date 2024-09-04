// license:BSD-3-Clause
// copyright-holders:Nicola Salmoria
/***************************************************************************

Mr Do!
driver by Nicola Salmoria

PCB Model: 8201
Main Clock: XTAL = 8.2 MHz
Video clock: XTAL = 19.6 MHz

Horizontal video frequency: HSYNC = XTAL/4/312 = 15.7051282051 kHz
Video frequency: VSYNC = HSYNC/262 = 59.94323742 Hz
VBlank duration: 1/VSYNC * (70/262) = 4457 us

The manual for this model clearly shows above values in 'Misc' parts listings.
There's a chance that certain bootlegs might have the different 8/20 MHz XTALS.

Sound chips have custom label "U8106". Or "8106" or unlabeled with the original
label scratched off. They are presumedly SN76489. Note that Lady Bug's PCB S/N
is also 8106 and has the same sound chips.

***************************************************************************/

#include "emu.h"

#include "cpu/z80/z80.h"
#include "sound/sn76496.h"
#include "screen.h"
#include "speaker.h"

#include "emupal.h"
#include "tilemap.h"


namespace {

class mrdo_state : public driver_device
{
public:
	mrdo_state(machine_config const &mconfig, device_type type, char const *tag, bool const bypass_pal = true) :
		driver_device(mconfig, type, tag),
		m_maincpu(*this, "maincpu"),
		m_screen(*this, "screen"),
		m_gfxdecode(*this, "gfxdecode"),
		m_palette(*this, "palette"),
		m_sn76489(*this, "sn%u", 1U),
		m_fieldram(*this, "fieldram%u", 1U),
		m_colorram(*this, "colorram%u", 1U),
		m_spriteram(*this, "spriteram"),
		m_bypass_pal(bypass_pal)
	{ }

	void mrdo(machine_config &config, XTAL const mclk, XTAL const vclk);
	void mrdobl(machine_config &config);
	void mrlo(machine_config &config);

protected:
	// initialization
	virtual void machine_start() override;
	virtual void machine_reset() override;
	virtual void video_start() override;

	// protection
	void set_protection(bool const state) { m_pal_enabled = state; };

	virtual void update_protection(u8 data);
	u8 protection_r(address_space &space);
	void protection_w(u8 data);

	u8 m_pal_u001 = 0U;

private:
	// devices
	required_device<z80_device> m_maincpu;
	required_device<screen_device> m_screen;
	required_device<gfxdecode_device> m_gfxdecode;
	required_device<palette_device> m_palette;
	required_device_array<sn76489_device, 2> m_sn76489;

	// memory pointers
	required_shared_ptr_array<u8, 2> m_fieldram;
	required_shared_ptr_array<u8, 2> m_colorram;
	required_shared_ptr<u8> m_spriteram;

	// protection hack
	bool const m_bypass_pal;

	// video-related
	void vblank_w(int state);
	void irq_clear_w(int state);
	template <int const Which> void fieldram_w(offs_t offset, u8 data);
	template <int const Which> void colorram_w(offs_t offset, u8 data);
	void scrollx_w(u8 data);
	void scrolly_w(u8 data);
	void flipscreen_w(u8 data);
	template <int const Which> TILE_GET_INFO_MEMBER(get_tile_info);
	void palette(palette_device &palette) const;
	u32 screen_update(screen_device &screen, bitmap_ind16 &bitmap, rectangle const &cliprect);
	void draw_sprites(bitmap_ind16 &bitmap, rectangle const &cliprect);

	// address maps
	void mrdo_map(address_map &map);

	// internal state
	tilemap_t *m_tilemap[2]{};
	bool m_flipscreen = false;
	bool m_pal_enabled = true;
};

class mrdot_state : public mrdo_state
{
public:
	mrdot_state(machine_config const &mconfig, device_type type, char const *tag) :
		mrdo_state(mconfig, type, tag, false)
	{ }

protected:
	virtual void update_protection(u8 data) override;
};

void mrdo_state::machine_start()
{
	save_item(NAME(m_pal_u001));
}

void mrdo_state::machine_reset()
{
	// initial outputs are high on power-up
	m_pal_u001 = 0xff;
}

u8 mrdo_state::protection_r(address_space &space)
{
	u8 const *rom = memregion("maincpu")->base();

	if (m_pal_enabled)
		if (m_bypass_pal)
			return rom[m_maincpu->state_int(Z80_HL)];
		else
			return m_pal_u001;
	else
		return space.unmap();
}

void mrdo_state::protection_w(u8 data)
{
	if (m_pal_enabled)
		update_protection(data);
}

void mrdo_state::update_protection(u8 data)
{
	// clocked by the is really the falling edge of TRAM WE (8000-8fff)
	// the WE is latched on WAIT RESET2 (VBLANK or low 3 bits of horz counter)
	// the latch is preset by HC (bit 3) of horz counter
	// output is anded with $7e before comparing to regster HL in the z80 code
}

void mrdot_state::update_protection(u8 data)
{
	// protection, each write latches a new value on IC U001 (PAL16R6)
	u8 const i9 = BIT(data, 0);
	u8 const i8 = BIT(data, 1);
//	u8 const i7 = BIT(data, 2); // pin 7 not used in equations
	u8 const i6 = BIT(data, 3);
	u8 const i5 = BIT(data, 4);
	u8 const i4 = BIT(data, 5);
	u8 const i3 = BIT(data, 6);
	u8 const i2 = BIT(data, 7);

	// equations extracted from dump using jedutil
	u8 const t1 =    i2  & (1^i3) &    i4  & (1^i5) & (1^i6) & (1^i8) &    i9;
	u8 const t2 = (1^i2) & (1^i3) &    i4  &    i5  & (1^i6) &    i8  & (1^i9);
	u8 const t3 =    i2  &    i3  & (1^i4) & (1^i5) &    i6  & (1^i8) &    i9;
	u8 const t4 = (1^i2) &    i3  &    i4  & (1^i5) &    i6  &    i8  &    i9;

	u8 const r12 = 0;
	u8 const r13 = ( t1 )      << 1;
	u8 const r14 = ( t1 | t2 ) << 2;
	u8 const r15 = ( t1 | t3 ) << 3;
	u8 const r16 = ( t1 )      << 4;
	u8 const r17 = ( t1 | t3 ) << 5;
	u8 const r18 = ( t3 | t4 ) << 6;
	u8 const r19 = 0;

	m_pal_u001 = ~(r19 | r18 | r17 | r16 | r15 | r14 | r13 | r12);
}

void mrdo_state::irq_clear_w(int state)
{
	if (state)
		m_maincpu->set_input_line(INPUT_LINE_IRQ0, CLEAR_LINE);
}

void mrdo_state::vblank_w(int state)
{
	if (state)
		m_maincpu->set_input_line(INPUT_LINE_IRQ0, ASSERT_LINE);
	else
		irq_clear_w(true);
}

/*
void mrdo_state::wait_timer(
{
	if (m_screen
}

template <int const Chip>
void mrdo_state::psg_ready(int state)
{
	m_psg_ready[Chip] = state;
	if (m_psg_ready[Chip] != state)
		refresh_wait();
}

void mrdo_state::refresh_wait()
{
	bool const wait_tvram = 
	bool const wait_a12 = 
	bool const wait_psg = m_psg_ready[0] || m_psg_ready[1];
	bool const state = wait1 || wait2 || wait3;

	m_maincpu->set_input_line(Z80_INPUT_LINE_WAIT, state ? ASSERT_LINE : CLEAR_LINE);
}
*/


void mrdo_state::mrdo_map(address_map &map)
{
	map(0x0000, 0x7fff).rom();
	map(0x8000, 0x83ff).ram().w(FUNC(mrdo_state::colorram_w<0>)).share(m_colorram[0]);
	map(0x8400, 0x87ff).ram().w(FUNC(mrdo_state::fieldram_w<0>)).share(m_fieldram[0]);
	map(0x8800, 0x8bff).ram().w(FUNC(mrdo_state::colorram_w<1>)).share(m_colorram[1]);
	map(0x8c00, 0x8fff).ram().w(FUNC(mrdo_state::fieldram_w<1>)).share(m_fieldram[1]);
	map(0x9000, 0x90ff).mirror(0x0700).writeonly().share(m_spriteram);
	map(0x9800, 0x9800).mirror(0x07f8).w(FUNC(mrdo_state::flipscreen_w)); // screen flip + playfield priority
	map(0x9801, 0x9801).mirror(0x07f8).w(m_sn76489[0], FUNC(sn76489_device::write));
	map(0x9802, 0x9802).mirror(0x07f8).w(m_sn76489[1], FUNC(sn76489_device::write));
	map(0x9803, 0x9803).mirror(0x07f8).r(FUNC(mrdo_state::protection_r));
	map(0x9804, 0x9807).mirror(0x07f8).nopr(); // decoded but unused
	map(0xa000, 0xa000).mirror(0x0ff8).portr("P1");
	map(0xa001, 0xa001).mirror(0x0ff8).portr("P2");
	map(0xa002, 0xa002).mirror(0x0ff8).portr("DSW1");
	map(0xa003, 0xa003).mirror(0x0ff8).portr("DSW2");
	map(0xa004, 0xa007).mirror(0x0ff8).nopr(); // decoded but unused
	// (0xb000, 0xdfff) - decoded but unused
	map(0xe000, 0xefff).ram();
	map(0xf000, 0xf000).mirror(0x07ff).w(FUNC(mrdo_state::scrollx_w));
	map(0xf800, 0xf800).mirror(0x07ff).w(FUNC(mrdo_state::scrolly_w));
}


/***************************************************************************

  Convert the color PROMs into a more useable format.

  Mr. Do! has two 32 bytes palette PROM and a 32 bytes sprite color lookup
  table PROM.
  The palette PROMs are connected to the RGB output this way:

  U2:
  bit 7 -- unused
        -- unused
        -- 100 ohm resistor  -diode- BLUE
        --  75 ohm resistor  -diode- BLUE
        -- 100 ohm resistor  -diode- GREEN
        --  75 ohm resistor  -diode- GREEN
        -- 100 ohm resistor  -diode- RED
  bit 0 --  75 ohm resistor  -diode- RED

  T2:
  bit 7 -- unused
        -- unused
        -- 150 ohm resistor  -diode- BLUE
        -- 120 ohm resistor  -diode- BLUE
        -- 150 ohm resistor  -diode- GREEN
        -- 120 ohm resistor  -diode- GREEN
        -- 150 ohm resistor  -diode- RED
  bit 0 -- 120 ohm resistor  -diode- RED

  200 ohm pulldown on all three components

***************************************************************************/

void mrdo_state::palette(palette_device &palette) const
{
	int constexpr R1          = 150;
	int constexpr R2          = 120;
	int constexpr R3          = 100;
	int constexpr R4          = 75;
	int constexpr pull        = 220;
	float constexpr potadjust = 0.7f; // diode voltage drop

	float pot[16];
	int weight[16];
	for (int i = 0x0f; i >= 0; i--)
	{
		float par = 0;

		if (i & 1) par += 1.0f / float(R1);
		if (i & 2) par += 1.0f / float(R2);
		if (i & 4) par += 1.0f / float(R3);
		if (i & 8) par += 1.0f / float(R4);
		if (par)
		{
			par = 1 / par;
			pot[i] = pull / (pull + par) - potadjust;
		}
		else
		{
			pot[i] = 0;
		}

		weight[i] = 0xff * pot[i] / pot[0x0f];
		if (weight[i] < 0)
			weight[i] = 0;
	}

	u8 const *prom = memregion("proms")->base();
	for (int i = 0; i < 0x100; i++)
	{
		int bits0, bits2;

		int const a1 = ((i >> 3) & 0x1c) + (i & 0x03) + 0x20;
		int const a2 = ((i >> 0) & 0x1c) + (i & 0x03);

		// red component
		bits0 = (prom[a1] >> 0) & 0x03;
		bits2 = (prom[a2] >> 0) & 0x03;
		int const r = weight[bits0 + (bits2 << 2)];

		// green component
		bits0 = (prom[a1] >> 2) & 0x03;
		bits2 = (prom[a2] >> 2) & 0x03;
		int const g = weight[bits0 + (bits2 << 2)];

		// blue component
		bits0 = (prom[a1] >> 4) & 0x03;
		bits2 = (prom[a2] >> 4) & 0x03;
		int const b = weight[bits0 + (bits2 << 2)];

		palette.set_indirect_color(i, rgb_t(r, g, b));
	}

	// point to the beginning of the lookup table
	prom += 0x40;

	// characters
	for (int i = 0; i < 0x100; i++)
		palette.set_pen_indirect(i, i);

	// sprites
	for (int i = 0; i < 0x40; i++)
	{
		u8 ctabentry = prom[i & 0x1f];

		if (i & 0x20)
			ctabentry >>= 4;   // high 4 bits are for sprite color n + 8
		else
			ctabentry &= 0x0f; // low 4 bits are for sprite color n

		palette.set_pen_indirect(i + 0x100, ctabentry + ((ctabentry & 0x0c) << 3));
	}
}


/***************************************************************************

  Callbacks for the TileMap code

***************************************************************************/

template <int const Which>
TILE_GET_INFO_MEMBER(mrdo_state::get_tile_info)
{
	u8 const attr     = m_colorram[Which][tile_index];
	bool const bank   = BIT(attr, 7);
	bool const opaque = BIT(attr, 6);
	u8 const color    = attr & 0x3f;
	u16 const code    = m_fieldram[Which][tile_index] | (bank << 8);
	int const flags   = opaque ? TILE_FORCE_LAYER0 : 0;

	tileinfo.set(Which, code, color, flags);
}

void mrdo_state::video_start()
{
	m_tilemap[0] = &machine().tilemap().create(*m_gfxdecode, tilemap_get_info_delegate(*this, FUNC(mrdo_state::get_tile_info<0>)), TILEMAP_SCAN_ROWS, 8, 8, 32, 32);
	m_tilemap[1] = &machine().tilemap().create(*m_gfxdecode, tilemap_get_info_delegate(*this, FUNC(mrdo_state::get_tile_info<1>)), TILEMAP_SCAN_ROWS, 8, 8, 32, 32);

	m_tilemap[0]->set_transparent_pen(0);
	m_tilemap[1]->set_transparent_pen(0);

	m_flipscreen = false;
	save_item(NAME(m_flipscreen));
}


/***************************************************************************

  Video memory handlers

***************************************************************************/

template <int const Which>
void mrdo_state::fieldram_w(offs_t offset, u8 data)
{
	if (m_fieldram[Which][offset] != data)
	{
		m_fieldram[Which][offset] = data;
		m_tilemap[Which]->mark_tile_dirty(offset);
	}

	// TVRAM writes trigger protection
	protection_w(data);
}

template <int const Which>
void mrdo_state::colorram_w(offs_t offset, u8 data)
{
	if (m_colorram[Which][offset] != data)
	{
		m_colorram[Which][offset] = data;
		m_tilemap[Which]->mark_tile_dirty(offset);
	}

	// TVRAM writes trigger protection
	protection_w(data);
}

void mrdo_state::scrollx_w(u8 data)
{
	m_tilemap[0]->set_scrollx(0, data);
}

void mrdo_state::scrolly_w(u8 data)
{
	// This is NOT affected by flipscreen (so stop it happening)
	if (m_flipscreen)
		m_tilemap[0]->set_scrolly(0, ((256 - data) & 0xff));
	else
		m_tilemap[0]->set_scrolly(0, data);
}

void mrdo_state::flipscreen_w(u8 data)
{
	// bits 1-3 control the playfield priority, but they are not used by
	// Mr. Do! so we don't emulate them
	m_flipscreen = BIT(data, 0);
	machine().tilemap().set_flip_all(m_flipscreen ? (TILEMAP_FLIPY | TILEMAP_FLIPX) : 0);
}


/***************************************************************************

  Display refresh

***************************************************************************/

void mrdo_state::draw_sprites(bitmap_ind16 &bitmap, rectangle const &cliprect)
{
	for (int offs = m_spriteram.bytes() - 4; offs >= 0; offs -= 4)
	{
		u8 const *src = &m_spriteram[offs];
		if (src[1] != 0)
		{
			u8 const code    = src[0];
			int const ypos   = 256 - src[1];
			u8 const attr    = src[2];
			u8 const color   = attr & 0xf;
			bool const flipx = BIT(attr, 4);
			bool const flipy = BIT(attr, 5);
			int const xpos   = src[3];

			m_gfxdecode->gfx(2)->transpen(bitmap, cliprect, code, color, flipx, flipy, xpos, ypos, 0);
		}
	}
}

u32 mrdo_state::screen_update(screen_device &screen, bitmap_ind16 &bitmap, rectangle const &cliprect)
{
	bitmap.fill(0, cliprect);
	m_tilemap[0]->draw(screen, bitmap, cliprect, 0, 0);
	m_tilemap[1]->draw(screen, bitmap, cliprect, 0, 0);
	draw_sprites(bitmap, cliprect);

	return 0;
}


static INPUT_PORTS_START( mrdo )
	PORT_START("P1")
	PORT_BIT( 0x01, IP_ACTIVE_LOW, IPT_JOYSTICK_LEFT )  PORT_4WAY
	PORT_BIT( 0x02, IP_ACTIVE_LOW, IPT_JOYSTICK_DOWN )  PORT_4WAY
	PORT_BIT( 0x04, IP_ACTIVE_LOW, IPT_JOYSTICK_RIGHT ) PORT_4WAY
	PORT_BIT( 0x08, IP_ACTIVE_LOW, IPT_JOYSTICK_UP )    PORT_4WAY
	PORT_BIT( 0x10, IP_ACTIVE_LOW, IPT_BUTTON1 )
	PORT_BIT( 0x20, IP_ACTIVE_LOW, IPT_START1 )
	PORT_BIT( 0x40, IP_ACTIVE_LOW, IPT_START2 )
	PORT_BIT( 0x80, IP_ACTIVE_LOW, IPT_TILT )

	PORT_START("P2")
	PORT_BIT( 0x01, IP_ACTIVE_LOW, IPT_JOYSTICK_LEFT )  PORT_4WAY PORT_COCKTAIL
	PORT_BIT( 0x02, IP_ACTIVE_LOW, IPT_JOYSTICK_DOWN )  PORT_4WAY PORT_COCKTAIL
	PORT_BIT( 0x04, IP_ACTIVE_LOW, IPT_JOYSTICK_RIGHT ) PORT_4WAY PORT_COCKTAIL
	PORT_BIT( 0x08, IP_ACTIVE_LOW, IPT_JOYSTICK_UP )    PORT_4WAY PORT_COCKTAIL
	PORT_BIT( 0x10, IP_ACTIVE_LOW, IPT_BUTTON1 )                  PORT_COCKTAIL
	PORT_BIT( 0x20, IP_ACTIVE_LOW, IPT_UNUSED )
	PORT_BIT( 0x40, IP_ACTIVE_LOW, IPT_COIN1 )
	PORT_BIT( 0x80, IP_ACTIVE_LOW, IPT_COIN2 )

	PORT_START("DSW1")
	PORT_DIPNAME( 0x03, 0x03, DEF_STR( Difficulty ) )                    PORT_DIPLOCATION("SW1:8,7")
	PORT_DIPSETTING(    0x03, DEF_STR( Easy ) )
	PORT_DIPSETTING(    0x02, DEF_STR( Medium ) )
	PORT_DIPSETTING(    0x01, DEF_STR( Hard ) )
	PORT_DIPSETTING(    0x00, DEF_STR( Hardest ) )
	PORT_DIPNAME( 0x04, 0x04, "Rack Test (Cheat)") PORT_CODE(KEYCODE_F1) PORT_DIPLOCATION("SW1:6")
	PORT_DIPSETTING(    0x04, DEF_STR( Off ) )
	PORT_DIPSETTING(    0x00, DEF_STR( On ) )
	PORT_DIPNAME( 0x08, 0x08, "Special" )                                PORT_DIPLOCATION("SW1:5")
	PORT_DIPSETTING(    0x08, DEF_STR( Easy ) )
	PORT_DIPSETTING(    0x00, DEF_STR( Hard ) )
	PORT_DIPNAME( 0x10, 0x10, "Extra" )                                  PORT_DIPLOCATION("SW1:4")
	PORT_DIPSETTING(    0x10, DEF_STR( Easy ) )
	PORT_DIPSETTING(    0x00, DEF_STR( Hard ) )
	PORT_DIPNAME( 0x20, 0x00, DEF_STR( Cabinet ) )                       PORT_DIPLOCATION("SW1:3")
	PORT_DIPSETTING(    0x00, DEF_STR( Upright ) )
	PORT_DIPSETTING(    0x20, DEF_STR( Cocktail ) )
	PORT_DIPNAME( 0xc0, 0xc0, DEF_STR( Lives ) )                         PORT_DIPLOCATION("SW1:2,1")
	PORT_DIPSETTING(    0x00, "2" )
	PORT_DIPSETTING(    0xc0, "3" )
	PORT_DIPSETTING(    0x80, "4" )
	PORT_DIPSETTING(    0x40, "5" )

	PORT_START("DSW2")
	PORT_DIPNAME( 0x0f, 0x0f, DEF_STR( Coin_B ) ) PORT_DIPLOCATION("SW2:8,7,6,5")
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
	// settings 0x01 through 0x05 all give 1 Coin/1 Credit
	PORT_DIPNAME( 0xf0, 0xf0, DEF_STR( Coin_A ) ) PORT_DIPLOCATION("SW2:4,3,2,1")
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
	// settings 0x10 through 0x50 all give 1 Coin/1 Credit
INPUT_PORTS_END


static gfx_layout const char_layout =
{
	8, 8,
	RGN_FRAC(1, 2),
	2,
	{ RGN_FRAC(0, 2), RGN_FRAC(1, 2) },
	{ STEP8(7, -1) },
	{ STEP8(0, 8) },
	8 * 8
};

static const gfx_layout sprite_layout =
{
	16, 16,
	RGN_FRAC(1, 1),
	2,
	{ 4, 0 },
	{ STEP4(0 * 8 + 3, -1), STEP4(1 * 8 + 3, -1),
	  STEP4(2 * 8 + 3, -1), STEP4(3 * 8 + 3, -1) },
	{ STEP16(0, 32) },
	64 * 8
};

static GFXDECODE_START( gfx_mrdo )
	GFXDECODE_ENTRY( "bgtiles", 0, char_layout,        0, 64 ) // colors 0-255 directly mapped
	GFXDECODE_ENTRY( "fgtiles", 0, char_layout,        0, 64 )
	GFXDECODE_ENTRY( "sprites", 0, sprite_layout, 4 * 64, 16 )
GFXDECODE_END


void mrdo_state::mrdo(machine_config &config, XTAL const mclk = 8.2_MHz_XTAL, XTAL const vclk = 19.6_MHz_XTAL)
{
	// basic machine hardware
	Z80(config, m_maincpu, mclk / 2); // verified
	m_maincpu->set_addrmap(AS_PROGRAM, &mrdo_state::mrdo_map);
	m_maincpu->irqack_cb().set(FUNC(mrdo_state::irq_clear_w));

	// video hardware
	SCREEN(config, m_screen, SCREEN_TYPE_RASTER);
	m_screen->set_raw(vclk / 4, 312, 8, 248, 262, 32, 224);
	m_screen->set_screen_update(FUNC(mrdo_state::screen_update));
	m_screen->screen_vblank().set(FUNC(mrdo_state::vblank_w));
	m_screen->set_palette(m_palette);

	GFXDECODE(config, m_gfxdecode, m_palette, gfx_mrdo);
	PALETTE(config, m_palette, FUNC(mrdo_state::palette), 64 * 4 + 16 * 4, 256);

	// sound hardware
	SPEAKER(config, "mono").front_center();

	SN76489(config, m_sn76489[0], mclk / 2).add_route(ALL_OUTPUTS, "mono", 0.50); // verified
	SN76489(config, m_sn76489[1], mclk / 2).add_route(ALL_OUTPUTS, "mono", 0.50); // verified
}

void mrdo_state::mrdobl(machine_config &config)
{
	mrdo(config, 8_MHz_XTAL, 19.908_MHz_XTAL);
}

void mrdo_state::mrlo(machine_config &config)
{
	mrdo(config);

	set_protection(false);
}


/***************************************************************************

  Game driver(s)

***************************************************************************/

ROM_START( mrdo )
	ROM_REGION( 0x10000, "maincpu", 0 )
	ROM_LOAD( "a4-01.bin",    0x0000, 0x2000, CRC(03dcfba2) SHA1(c15e3d0c4225e0ca120bcd28aca39632575f8e11) )
	ROM_LOAD( "c4-02.bin",    0x2000, 0x2000, CRC(0ecdd39c) SHA1(c64b3363593911a676c647bf3dba8fe063fcb0de) )
	ROM_LOAD( "e4-03.bin",    0x4000, 0x2000, CRC(358f5dc2) SHA1(9fed1f5d1d04935d1b77687c8b2f3bfce970dc08) )
	ROM_LOAD( "f4-04.bin",    0x6000, 0x2000, CRC(f4190cfc) SHA1(24f5125d900f944294d4eda068b710c8f1c6d39f) )

	ROM_REGION( 0x2000, "fgtiles", 0 )
	ROM_LOAD( "s8-09.bin",    0x0000, 0x1000, CRC(aa80c5b6) SHA1(76f9f90deb74598470e7ed565237da38dd07e4e9) )
	ROM_LOAD( "u8-10.bin",    0x1000, 0x1000, CRC(d20ec85b) SHA1(9762bbe34d3fa209ea719807c723f57cb6bf4e01) )

	ROM_REGION( 0x2000, "bgtiles", 0 )
	ROM_LOAD( "r8-08.bin",    0x0000, 0x1000, CRC(dbdc9ffa) SHA1(93f29fc106283eecbba3fd69cf3c4658aa38ab9f) )
	ROM_LOAD( "n8-07.bin",    0x1000, 0x1000, CRC(4b9973db) SHA1(8766c51a345a5e63446e65614c6f665ab5fbe0d7) )

	ROM_REGION( 0x2000, "sprites", 0 )
	ROM_LOAD( "h5-05.bin",    0x0000, 0x1000, CRC(e1218cc5) SHA1(d946613a1cf1c97f7533a4f8c2d0078d1b7daaa8) )
	ROM_LOAD( "k5-06.bin",    0x1000, 0x1000, CRC(b1f68b04) SHA1(25709cd81c03df51f27cd730fecf86a1daa9e27e) )

	ROM_REGION( 0x0080, "proms", 0 )
	ROM_LOAD( "u02--2.bin",   0x0000, 0x0020, CRC(238a65d7) SHA1(a5b20184a1989db23544296331462ec4d7be7516) ) // palette (high bits)
	ROM_LOAD( "t02--3.bin",   0x0020, 0x0020, CRC(ae263dc0) SHA1(7072c100b9d692f5bb12b0c9e304425f534481e2) ) // palette (low bits)
	ROM_LOAD( "f10--1.bin",   0x0040, 0x0020, CRC(16ee4ca2) SHA1(fcba4d103708b9711452009cd29c4f88d2f64cd3) ) // sprite color lookup table
	ROM_LOAD( "j10--4.bin",   0x0060, 0x0020, CRC(ff7fe284) SHA1(3ac8e30011c1fcba0ee8f4dc932f82296c3ba143) ) // timing (not used)

	ROM_REGION( 0x0200, "pal16r6", 0 )
	ROM_LOAD( "u001_pal16r6cn.j2", 0x0000, 0x0104, CRC(84dbe498) SHA1(5863342b2db85ffef31b5e9ce26bfd8fca9923b0) )
ROM_END

ROM_START( mrdoy )
	ROM_REGION( 0x10000, "maincpu", 0 )
	ROM_LOAD( "dosnow.1",     0x0000, 0x2000, CRC(d3454e2c) SHA1(f8ecb9eec414badbcb65b7188d4a4d06739534cc) )
	ROM_LOAD( "dosnow.2",     0x2000, 0x2000, CRC(5120a6b2) SHA1(1db6dc3a91ac024e763179f425ad46d9d0aff8f9) )
	ROM_LOAD( "dosnow.3",     0x4000, 0x2000, CRC(96416dbe) SHA1(55f5262448b65899309f3e9e16c62b0c1e0b86c3) )
	ROM_LOAD( "dosnow.4",     0x6000, 0x2000, CRC(c05051b6) SHA1(6f528370dc097bf1550f4fa4b5f740214bc18f0b) )

	ROM_REGION( 0x2000, "fgtiles", 0 )
	ROM_LOAD( "dosnow.9",     0x0000, 0x1000, CRC(85d16217) SHA1(35cb4e4a9e55f42f7818aeaa3f72892d2ddc99aa) )
	ROM_LOAD( "dosnow.10",    0x1000, 0x1000, CRC(61a7f54b) SHA1(19b0074f098955d61e5dfab060873ac96fdb30b4) )

	ROM_REGION( 0x2000, "bgtiles", 0 )
	ROM_LOAD( "dosnow.8",     0x0000, 0x1000, CRC(2bd1239a) SHA1(43a36afbf7374578e9735956f54412823486b3ff) )
	ROM_LOAD( "dosnow.7",     0x1000, 0x1000, CRC(ac8ffddf) SHA1(9911524de6b4e9056944b92a53ac93de110d52bd) )

	ROM_REGION( 0x2000, "sprites", 0 )
	ROM_LOAD( "dosnow.5",     0x0000, 0x1000, CRC(7662d828) SHA1(559150326e3edc7ee062bfd962fe8d39f9423b45) )
	ROM_LOAD( "dosnow.6",     0x1000, 0x1000, CRC(413f88d1) SHA1(830df0def7289536e2d08e0517cdb6edbc947400) )

	ROM_REGION( 0x0080, "proms", 0 )
	ROM_LOAD( "u02--2.bin",   0x0000, 0x0020, CRC(238a65d7) SHA1(a5b20184a1989db23544296331462ec4d7be7516) ) // palette (high bits)
	ROM_LOAD( "t02--3.bin",   0x0020, 0x0020, CRC(ae263dc0) SHA1(7072c100b9d692f5bb12b0c9e304425f534481e2) ) // palette (low bits)
	ROM_LOAD( "f10--1.bin",   0x0040, 0x0020, CRC(16ee4ca2) SHA1(fcba4d103708b9711452009cd29c4f88d2f64cd3) ) // sprite color lookup table
	ROM_LOAD( "j10--4.bin",   0x0060, 0x0020, CRC(ff7fe284) SHA1(3ac8e30011c1fcba0ee8f4dc932f82296c3ba143) ) // timing (not used)

	ROM_REGION( 0x0200, "pal16r6", 0 )
	ROM_LOAD( "u001_pal16r6cn.j2", 0x0000, 0x0104, CRC(84dbe498) SHA1(5863342b2db85ffef31b5e9ce26bfd8fca9923b0) )
ROM_END

ROM_START( mrdobl )
	ROM_REGION( 0x10000, "maincpu", 0 )
	ROM_LOAD( "d1.a4",  0x0000, 0x2000, CRC(03dcfba2) SHA1(c15e3d0c4225e0ca120bcd28aca39632575f8e11) )
	ROM_LOAD( "d2.c4",  0x2000, 0x2000, CRC(0ecdd39c) SHA1(c64b3363593911a676c647bf3dba8fe063fcb0de) )
	ROM_LOAD( "d3.e4",  0x4000, 0x2000, CRC(afc518e3) SHA1(abfb874e22ce375a2badecafaf95a3cd8c6179b1) )
	ROM_LOAD( "d4.f4",  0x6000, 0x2000, CRC(f4190cfc) SHA1(24f5125d900f944294d4eda068b710c8f1c6d39f) )

	ROM_REGION( 0x2000, "fgtiles", 0 )
	ROM_LOAD( "d9.s8",  0x0000, 0x1000, CRC(aa80c5b6) SHA1(76f9f90deb74598470e7ed565237da38dd07e4e9) )
	ROM_LOAD( "d10.u8", 0x1000, 0x1000, CRC(d20ec85b) SHA1(9762bbe34d3fa209ea719807c723f57cb6bf4e01) )

	ROM_REGION( 0x2000, "bgtiles", 0 )
	ROM_LOAD( "d8.r8",  0x0000, 0x1000, CRC(dbdc9ffa) SHA1(93f29fc106283eecbba3fd69cf3c4658aa38ab9f) )
	ROM_LOAD( "d7.n8",  0x1000, 0x1000, CRC(4b9973db) SHA1(8766c51a345a5e63446e65614c6f665ab5fbe0d7) )

	ROM_REGION( 0x2000, "sprites", 0 )
	ROM_LOAD( "d5.h5",  0x0000, 0x1000, CRC(e1218cc5) SHA1(d946613a1cf1c97f7533a4f8c2d0078d1b7daaa8) )
	ROM_LOAD( "d6.k5",  0x1000, 0x1000, CRC(b1f68b04) SHA1(25709cd81c03df51f27cd730fecf86a1daa9e27e) )

	ROM_REGION( 0x0080, "proms", 0 )
	ROM_LOAD( "2_18s030.u2", 0x0000, 0x0020, CRC(238a65d7) SHA1(a5b20184a1989db23544296331462ec4d7be7516) ) // palette (high bits)
	ROM_LOAD( "1_18s030.t2", 0x0020, 0x0020, CRC(ae263dc0) SHA1(7072c100b9d692f5bb12b0c9e304425f534481e2) ) // palette (low bits)
	ROM_LOAD( "7603-5.e10",  0x0040, 0x0020, CRC(16ee4ca2) SHA1(fcba4d103708b9711452009cd29c4f88d2f64cd3) ) // sprite color lookup table
	ROM_LOAD( "82s123.j10",  0x0060, 0x0020, CRC(ff7fe284) SHA1(3ac8e30011c1fcba0ee8f4dc932f82296c3ba143) ) // timing (not used)

	ROM_REGION( 0x0104, "pal16r6", 0 )
	ROM_LOAD( "u001_pal16r6cn.j2", 0x0000, 0x0104, CRC(84dbe498) SHA1(5863342b2db85ffef31b5e9ce26bfd8fca9923b0) )
ROM_END

ROM_START( yankeedo )
	ROM_REGION( 0x10000, "maincpu", 0 )
	ROM_LOAD( "a4-01.bin",    0x0000, 0x2000, CRC(03dcfba2) SHA1(c15e3d0c4225e0ca120bcd28aca39632575f8e11) )
	ROM_LOAD( "yd_d2.c4",     0x2000, 0x2000, CRC(7c9d7ce0) SHA1(37889575c7c83cb647008b038e4efdc87355bd3e) )
	ROM_LOAD( "e4-03.bin",    0x4000, 0x2000, CRC(358f5dc2) SHA1(9fed1f5d1d04935d1b77687c8b2f3bfce970dc08) )
	ROM_LOAD( "f4-04.bin",    0x6000, 0x2000, CRC(f4190cfc) SHA1(24f5125d900f944294d4eda068b710c8f1c6d39f) )

	ROM_REGION( 0x2000, "fgtiles", 0 )
	ROM_LOAD( "s8-09.bin",    0x0000, 0x1000, CRC(aa80c5b6) SHA1(76f9f90deb74598470e7ed565237da38dd07e4e9) )
	ROM_LOAD( "u8-10.bin",    0x1000, 0x1000, CRC(d20ec85b) SHA1(9762bbe34d3fa209ea719807c723f57cb6bf4e01) )

	ROM_REGION( 0x2000, "bgtiles", 0 )
	ROM_LOAD( "r8-08.bin",    0x0000, 0x1000, CRC(dbdc9ffa) SHA1(93f29fc106283eecbba3fd69cf3c4658aa38ab9f) )
	ROM_LOAD( "n8-07.bin",    0x1000, 0x1000, CRC(4b9973db) SHA1(8766c51a345a5e63446e65614c6f665ab5fbe0d7) )

	ROM_REGION( 0x2000, "sprites", 0 )
	ROM_LOAD( "yd_d5.h5",     0x0000, 0x1000, CRC(f530b79b) SHA1(bffc4ddf8aa26933c8a15ed40bfa0b4cee85b408) )
	ROM_LOAD( "yd_d6.k5",     0x1000, 0x1000, CRC(790579aa) SHA1(89d8a77d2046cf8cfc393e0f08d361d1886bfec1) )

	ROM_REGION( 0x0080, "proms", 0 )
	ROM_LOAD( "u02--2.bin",   0x0000, 0x0020, CRC(238a65d7) SHA1(a5b20184a1989db23544296331462ec4d7be7516) ) // palette (high bits)
	ROM_LOAD( "t02--3.bin",   0x0020, 0x0020, CRC(ae263dc0) SHA1(7072c100b9d692f5bb12b0c9e304425f534481e2) ) // palette (low bits)
	ROM_LOAD( "f10--1.bin",   0x0040, 0x0020, CRC(16ee4ca2) SHA1(fcba4d103708b9711452009cd29c4f88d2f64cd3) ) // sprite color lookup table
	ROM_LOAD( "j10--4.bin",   0x0060, 0x0020, CRC(ff7fe284) SHA1(3ac8e30011c1fcba0ee8f4dc932f82296c3ba143) ) // timing (not used)

	ROM_REGION( 0x0200, "pal16r6", 0 )
	ROM_LOAD( "u001_pal16r6cn.j2", 0x0000, 0x0104, CRC(84dbe498) SHA1(5863342b2db85ffef31b5e9ce26bfd8fca9923b0) )
ROM_END


ROM_START( mrdot )
	ROM_REGION( 0x10000, "maincpu", 0 )
	ROM_LOAD( "d1",           0x0000, 0x2000, CRC(3dcd9359) SHA1(bfe00450ee8822f437d87514f051ad1be6de9463) )
	ROM_LOAD( "d2",           0x2000, 0x2000, CRC(710058d8) SHA1(168cc179f2266bbf9437445bef9ff7d3358a8e6b) )
	ROM_LOAD( "d3",           0x4000, 0x2000, CRC(467d12d8) SHA1(7bb85e6a780de1c0c224229ee571cab39098f78d) )
	ROM_LOAD( "d4",           0x6000, 0x2000, CRC(fce9afeb) SHA1(26236a42c1c620975d4480c4315d0c6f112429b6) )

	ROM_REGION( 0x2000, "fgtiles", 0 )
	ROM_LOAD( "d9",           0x0000, 0x1000, CRC(de4cfe66) SHA1(c217dcc24305f3b4badfb778a1cf4e57c178d168) )
	ROM_LOAD( "d10",          0x1000, 0x1000, CRC(a6c2f38b) SHA1(7c132771bf385c8ed28d8c8bdfc3dbf0b4aa75e8) )

	ROM_REGION( 0x2000, "bgtiles", 0 )
	ROM_LOAD( "r8-08.bin",    0x0000, 0x1000, CRC(dbdc9ffa) SHA1(93f29fc106283eecbba3fd69cf3c4658aa38ab9f) )
	ROM_LOAD( "n8-07.bin",    0x1000, 0x1000, CRC(4b9973db) SHA1(8766c51a345a5e63446e65614c6f665ab5fbe0d7) )

	ROM_REGION( 0x2000, "sprites", 0 )
	ROM_LOAD( "h5-05.bin",    0x0000, 0x1000, CRC(e1218cc5) SHA1(d946613a1cf1c97f7533a4f8c2d0078d1b7daaa8) )
	ROM_LOAD( "k5-06.bin",    0x1000, 0x1000, CRC(b1f68b04) SHA1(25709cd81c03df51f27cd730fecf86a1daa9e27e) )

	ROM_REGION( 0x0080, "proms", 0 )
	ROM_LOAD( "u02--2.bin",   0x0000, 0x0020, CRC(238a65d7) SHA1(a5b20184a1989db23544296331462ec4d7be7516) ) // palette (high bits)
	ROM_LOAD( "t02--3.bin",   0x0020, 0x0020, CRC(ae263dc0) SHA1(7072c100b9d692f5bb12b0c9e304425f534481e2) ) // palette (low bits)
	ROM_LOAD( "f10--1.bin",   0x0040, 0x0020, CRC(16ee4ca2) SHA1(fcba4d103708b9711452009cd29c4f88d2f64cd3) ) // sprite color lookup table
	ROM_LOAD( "j10--4.bin",   0x0060, 0x0020, CRC(ff7fe284) SHA1(3ac8e30011c1fcba0ee8f4dc932f82296c3ba143) ) // timing (not used)

	ROM_REGION( 0x0200, "pal16r6", 0 )
	ROM_LOAD( "j2-u001.bin",  0x0000, 0x0117, CRC(badf5876) SHA1(b301cfc7f8e83408fdcb742f552a0414af6aa16e) ) // PAL16R6 converted to GAL16V8
ROM_END

ROM_START( mrdofix )
	ROM_REGION( 0x10000, "maincpu", 0 )
	ROM_LOAD( "d1",           0x0000, 0x2000, CRC(3dcd9359) SHA1(bfe00450ee8822f437d87514f051ad1be6de9463) )
	ROM_LOAD( "d2",           0x2000, 0x2000, CRC(710058d8) SHA1(168cc179f2266bbf9437445bef9ff7d3358a8e6b) )
	ROM_LOAD( "dofix.d3",     0x4000, 0x2000, CRC(3a7d039b) SHA1(ac87a3c9fa6433d1700e858914a995dce35113fa) )
	ROM_LOAD( "dofix.d4",     0x6000, 0x2000, CRC(32db845f) SHA1(5c58532ae2cfab9bd81383824d970b20015c960e) )

	ROM_REGION( 0x2000, "fgtiles", 0 )
	ROM_LOAD( "d9",           0x0000, 0x1000, CRC(de4cfe66) SHA1(c217dcc24305f3b4badfb778a1cf4e57c178d168) )
	ROM_LOAD( "d10",          0x1000, 0x1000, CRC(a6c2f38b) SHA1(7c132771bf385c8ed28d8c8bdfc3dbf0b4aa75e8) )

	ROM_REGION( 0x2000, "bgtiles", 0 )
	ROM_LOAD( "r8-08.bin",    0x0000, 0x1000, CRC(dbdc9ffa) SHA1(93f29fc106283eecbba3fd69cf3c4658aa38ab9f) )
	ROM_LOAD( "n8-07.bin",    0x1000, 0x1000, CRC(4b9973db) SHA1(8766c51a345a5e63446e65614c6f665ab5fbe0d7) )

	ROM_REGION( 0x2000, "sprites", 0 )
	ROM_LOAD( "h5-05.bin",    0x0000, 0x1000, CRC(e1218cc5) SHA1(d946613a1cf1c97f7533a4f8c2d0078d1b7daaa8) )
	ROM_LOAD( "k5-06.bin",    0x1000, 0x1000, CRC(b1f68b04) SHA1(25709cd81c03df51f27cd730fecf86a1daa9e27e) )

	ROM_REGION( 0x0080, "proms", 0 )
	ROM_LOAD( "u02--2.bin",   0x0000, 0x0020, CRC(238a65d7) SHA1(a5b20184a1989db23544296331462ec4d7be7516) ) // palette (high bits)
	ROM_LOAD( "t02--3.bin",   0x0020, 0x0020, CRC(ae263dc0) SHA1(7072c100b9d692f5bb12b0c9e304425f534481e2) ) // palette (low bits)
	ROM_LOAD( "f10--1.bin",   0x0040, 0x0020, CRC(16ee4ca2) SHA1(fcba4d103708b9711452009cd29c4f88d2f64cd3) ) // sprite color lookup table
	ROM_LOAD( "j10--4.bin",   0x0060, 0x0020, CRC(ff7fe284) SHA1(3ac8e30011c1fcba0ee8f4dc932f82296c3ba143) ) // timing (not used)

	ROM_REGION( 0x0200, "pal16r6", 0 )
	ROM_LOAD( "j2-u001.bin",  0x0000, 0x0117, CRC(badf5876) SHA1(b301cfc7f8e83408fdcb742f552a0414af6aa16e) ) // PAL16R6 converted to GAL16V8
ROM_END

ROM_START( mrdu )
	ROM_REGION( 0x10000, "maincpu", 0 )
	ROM_LOAD( "d1",           0x0000, 0x2000, CRC(3dcd9359) SHA1(bfe00450ee8822f437d87514f051ad1be6de9463) )
	ROM_LOAD( "d2",           0x2000, 0x2000, CRC(710058d8) SHA1(168cc179f2266bbf9437445bef9ff7d3358a8e6b) )
	ROM_LOAD( "d3",           0x4000, 0x2000, CRC(467d12d8) SHA1(7bb85e6a780de1c0c224229ee571cab39098f78d) )
	ROM_LOAD( "du4.bin",      0x6000, 0x2000, CRC(893bc218) SHA1(2b546989c4eef9f94594c50a48458c91e3f4983f) )

	ROM_REGION( 0x2000, "fgtiles", 0 )
	ROM_LOAD( "du9.bin",      0x0000, 0x1000, CRC(4090dcdc) SHA1(7f481f2e966d6a98fd7d82404afefc1483658ffa) )
	ROM_LOAD( "du10.bin",     0x1000, 0x1000, CRC(1e63ab69) SHA1(f0a4a12f818bc21c2bf0fe755c2e378b968b977b) )

	ROM_REGION( 0x2000, "bgtiles", 0 )
	ROM_LOAD( "r8-08.bin",    0x0000, 0x1000, CRC(dbdc9ffa) SHA1(93f29fc106283eecbba3fd69cf3c4658aa38ab9f) )
	ROM_LOAD( "n8-07.bin",    0x1000, 0x1000, CRC(4b9973db) SHA1(8766c51a345a5e63446e65614c6f665ab5fbe0d7) )

	ROM_REGION( 0x2000, "sprites", 0 )
	ROM_LOAD( "h5-05.bin",    0x0000, 0x1000, CRC(e1218cc5) SHA1(d946613a1cf1c97f7533a4f8c2d0078d1b7daaa8) )
	ROM_LOAD( "k5-06.bin",    0x1000, 0x1000, CRC(b1f68b04) SHA1(25709cd81c03df51f27cd730fecf86a1daa9e27e) )

	ROM_REGION( 0x0080, "proms", 0 )
	ROM_LOAD( "u02--2.bin",   0x0000, 0x0020, CRC(238a65d7) SHA1(a5b20184a1989db23544296331462ec4d7be7516) ) // palette (high bits)
	ROM_LOAD( "t02--3.bin",   0x0020, 0x0020, CRC(ae263dc0) SHA1(7072c100b9d692f5bb12b0c9e304425f534481e2) ) // palette (low bits)
	ROM_LOAD( "f10--1.bin",   0x0040, 0x0020, CRC(16ee4ca2) SHA1(fcba4d103708b9711452009cd29c4f88d2f64cd3) ) // sprite color lookup table
	ROM_LOAD( "j10--4.bin",   0x0060, 0x0020, CRC(ff7fe284) SHA1(3ac8e30011c1fcba0ee8f4dc932f82296c3ba143) ) // timing (not used)

	ROM_REGION( 0x0200, "pal16r6", 0 )
	ROM_LOAD( "j2-u001.bin",  0x0000, 0x0117, CRC(badf5876) SHA1(b301cfc7f8e83408fdcb742f552a0414af6aa16e) ) // PAL16R6 converted to GAL16V8
ROM_END

// The white garbled graphics on the title screen should be the Fabremar logo (32px height), but it's drawn as
// 16px height, like the original Taito logo. Since the F4 ROM had a different label than the others and it matches
// with 'mrdot', someone probably replaced the original F4 Fabremar ROM with the one from Taito.
ROM_START( mrdofabr )
	ROM_REGION( 0x10000, "maincpu", 0 )
	ROM_LOAD( "md_fabre.a4", 0x0000, 0x2000, CRC(62593aed) SHA1(ac1cc4fa4ee3799e84938333a2a698d1ec0b527b) )
	ROM_LOAD( "md_fabre.b4", 0x2000, 0x2000, CRC(710058d8) SHA1(168cc179f2266bbf9437445bef9ff7d3358a8e6b) )
	ROM_LOAD( "md_fabre.c4", 0x4000, 0x2000, CRC(467d12d8) SHA1(7bb85e6a780de1c0c224229ee571cab39098f78d) )
	ROM_LOAD( "md_fabre.f4", 0x6000, 0x2000, BAD_DUMP CRC(fce9afeb) SHA1(26236a42c1c620975d4480c4315d0c6f112429b6) )

	ROM_REGION( 0x2000, "fgtiles", 0 )
	ROM_LOAD( "md_fabre.t8", 0x0000, 0x1000, CRC(f2dff901) SHA1(ddc3b38bfd8b822d7803ee51e2c13443b25e39ee) )
	ROM_LOAD( "md_fabre.u8", 0x1000, 0x1000, CRC(f3e443bd) SHA1(10e134962b0c7500f57d4058cbd0475f5c5fa6ab) )

	ROM_REGION( 0x2000, "bgtiles", 0 )
	ROM_LOAD( "md_fabre.r8", 0x0000, 0x1000, CRC(dbdc9ffa) SHA1(93f29fc106283eecbba3fd69cf3c4658aa38ab9f) )
	ROM_LOAD( "md_fabre.n8", 0x1000, 0x1000, CRC(4b9973db) SHA1(8766c51a345a5e63446e65614c6f665ab5fbe0d7) )

	ROM_REGION( 0x2000, "sprites", 0 )
	ROM_LOAD( "md_fabre.h5", 0x0000, 0x1000, CRC(e1218cc5) SHA1(d946613a1cf1c97f7533a4f8c2d0078d1b7daaa8) )
	ROM_LOAD( "md_fabre.k5", 0x1000, 0x1000, CRC(b1f68b04) SHA1(25709cd81c03df51f27cd730fecf86a1daa9e27e) )

	ROM_REGION( 0x0080, "proms", 0 )
	ROM_LOAD( "82s123.u2",   0x0000, 0x0020, CRC(238a65d7) SHA1(a5b20184a1989db23544296331462ec4d7be7516) ) // palette (high bits)
	ROM_LOAD( "82s123.t2",   0x0020, 0x0020, CRC(ae263dc0) SHA1(7072c100b9d692f5bb12b0c9e304425f534481e2) ) // palette (low bits)
	ROM_LOAD( "82s123.f10n", 0x0040, 0x0020, CRC(16ee4ca2) SHA1(fcba4d103708b9711452009cd29c4f88d2f64cd3) ) // sprite color lookup table
	ROM_LOAD( "82s123.j10",  0x0060, 0x0020, CRC(ff7fe284) SHA1(3ac8e30011c1fcba0ee8f4dc932f82296c3ba143) ) // timing (not used)

	ROM_REGION( 0x0200, "pal16r6", 0 )
	ROM_LOAD( "j2-u001.bin",  0x0000, 0x0117, CRC(badf5876) SHA1(b301cfc7f8e83408fdcb742f552a0414af6aa16e) ) // From mrdot, protected on this set
ROM_END


ROM_START( mrlo )
	ROM_REGION( 0x8000, "maincpu", 0 )
	ROM_LOAD( "mrlo01.bin",   0x0000, 0x2000, CRC(6f455e7d) SHA1(82fbe05229f19fb849c90b41e3365be74f4f448f) )
	ROM_LOAD( "d2",           0x2000, 0x2000, CRC(710058d8) SHA1(168cc179f2266bbf9437445bef9ff7d3358a8e6b) )
	ROM_LOAD( "dofix.d3",     0x4000, 0x2000, CRC(3a7d039b) SHA1(ac87a3c9fa6433d1700e858914a995dce35113fa) )
	ROM_LOAD( "mrlo04.bin",   0x6000, 0x2000, CRC(49c10274) SHA1(e94b638f9888ebdff114f80e2c5906bbc81d9c6b) )

	ROM_REGION( 0x2000, "fgtiles", 0 )
	ROM_LOAD( "mrlo09.bin",   0x0000, 0x1000, CRC(fdb60d0d) SHA1(fe3502058a68247e5a55b930136f8d0cb80f894f) )
	ROM_LOAD( "mrlo10.bin",   0x1000, 0x1000, CRC(0492c10e) SHA1(782e541539537ab3f3a590770ca48bdc0fabdc10) )

	ROM_REGION( 0x2000, "bgtiles", 0 )
	ROM_LOAD( "r8-08.bin",    0x0000, 0x1000, CRC(dbdc9ffa) SHA1(93f29fc106283eecbba3fd69cf3c4658aa38ab9f) )
	ROM_LOAD( "n8-07.bin",    0x1000, 0x1000, CRC(4b9973db) SHA1(8766c51a345a5e63446e65614c6f665ab5fbe0d7) )

	ROM_REGION( 0x2000, "sprites", 0 )
	ROM_LOAD( "h5-05.bin",    0x0000, 0x1000, CRC(e1218cc5) SHA1(d946613a1cf1c97f7533a4f8c2d0078d1b7daaa8) )
	ROM_LOAD( "k5-06.bin",    0x1000, 0x1000, CRC(b1f68b04) SHA1(25709cd81c03df51f27cd730fecf86a1daa9e27e) )

	ROM_REGION( 0x0080, "proms", 0 )
	ROM_LOAD( "u02--2.bin",   0x0000, 0x0020, CRC(238a65d7) SHA1(a5b20184a1989db23544296331462ec4d7be7516) ) // palette (high bits)
	ROM_LOAD( "t02--3.bin",   0x0020, 0x0020, CRC(ae263dc0) SHA1(7072c100b9d692f5bb12b0c9e304425f534481e2) ) // palette (low bits)
	ROM_LOAD( "f10--1.bin",   0x0040, 0x0020, CRC(16ee4ca2) SHA1(fcba4d103708b9711452009cd29c4f88d2f64cd3) ) // sprite color lookup table
	ROM_LOAD( "j10--4.bin",   0x0060, 0x0020, CRC(ff7fe284) SHA1(3ac8e30011c1fcba0ee8f4dc932f82296c3ba143) ) // timing (not used)
ROM_END

} // anonymous namespace


GAME( 1982, mrdo,     0,    mrdo,   mrdo, mrdo_state,  empty_init, ROT270, "Universal",                 "Mr. Do!",                    MACHINE_SUPPORTS_SAVE )
GAME( 1982, mrdoy,    mrdo, mrdo,   mrdo, mrdo_state,  empty_init, ROT270, "Universal",                 "Mr. Do! (prototype)",        MACHINE_SUPPORTS_SAVE ) // aka "Yukidaruma"
GAME( 1982, mrdobl,   mrdo, mrdobl, mrdo, mrdo_state,  empty_init, ROT270, "bootleg",                   "Mr. Do! (bootleg)",          MACHINE_SUPPORTS_SAVE )
GAME( 1982, yankeedo, mrdo, mrdo,   mrdo, mrdo_state,  empty_init, ROT270, "hack",                      "Yankee DO!",                 MACHINE_SUPPORTS_SAVE )

GAME( 1982, mrdot,    mrdo, mrdo,   mrdo, mrdot_state, empty_init, ROT270, "Universal (Taito license)", "Mr. Do! (Taito)",            MACHINE_SUPPORTS_SAVE )
GAME( 1982, mrdofix,  mrdo, mrdo,   mrdo, mrdot_state, empty_init, ROT270, "Universal (Taito license)", "Mr. Do! (bugfixed)",         MACHINE_SUPPORTS_SAVE )
GAME( 1982, mrdu,     mrdo, mrdo,   mrdo, mrdot_state, empty_init, ROT270, "bootleg",                   "Mr. Du!",                    MACHINE_SUPPORTS_SAVE )
GAME( 1982, mrdofabr, mrdo, mrdo,   mrdo, mrdot_state, empty_init, ROT270, "bootleg (Fabremar)",        "Mr. Do! (Fabremar bootleg)", MACHINE_SUPPORTS_SAVE )

GAME( 1982, mrlo,     mrdo, mrlo,   mrdo, mrdo_state,  empty_init, ROT270, "bootleg",                   "Mr. Lo!",                    MACHINE_SUPPORTS_SAVE ) // no protection
