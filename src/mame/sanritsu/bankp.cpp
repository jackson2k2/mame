// license:BSD-3-Clause
// copyright-holders: Nicola Salmoria
/***************************************************************************

    Bank Panic / Combat Hawk hardware
    Sega & Sanritsu
    1984 - 1987

    driver by Nicola Salmoria

    PCB footage:
    https://youtu.be/Ikz1t7iSQYc


    PCB Layout (Combat Hawk)
    -----------------------

    C2-00170-A BANK PANIC (screened on PCB)
    834-6381 COMBAT HAWK (sticker)
    SANRITSU VIDEO GAME - screened on PCB

|------------------------------------------------------------------------|
|                             PR-10902             PR-10900              |
|        PAL (315-5073)                                                  |
|                                                                        |
|        PAL (315-5074)       EPR-10907                                  |
|                                                                        |
|                       2016  EPR-10908            EPR-10903             |
|                                                                       2|
|                             EPR-10909            EPR-10904            2|
|                                                                       ||
|                             EPR-10910  PR-10901  EPR-10905   SW1      W|
|                                                                       A|
|                                                                       Y|
|                             EPR-10911  2016      EPR-10906   SN76489   |
|                                                                        |
|                                                              SN76489   |
|                             EPR-10912                                  |
|                                                              SN76489   |
|             2016            EPR-10913             Z80                  |
|                                                               VOL      |
|15.468MHz                    EPR-10914             555   358     HA1377A|
|------------------------------------------------------------------------|

    Notes:
          2016          - 2kx8 SRAM
          Z80 clock     - 2.578MHz [15.468/6]
          SN76489 clock - 2.578MHz [15.468/6]
          VSync         - 60Hz
          HSync         - 15.36kHz
          SW1           - 8-position DIP switch

          ROMs
          ----

          PR-10900         - ?
          PR-10901         - 82S129 BIPOLAR PROM
          PR-10902         - 82S129 BIPOLAR PROM
          EPR-10907-14 & 3 - 2764 EPROM
          EPR-10904-6      - 27128 EPROM
          315-5073         - PAL16L4 (protected)
          315-5074         - PAL10L8 (read OK)

---------------------------------------------------

    Bank Panic memory map (preliminary)
    Similar to Appoooh

    0000-dfff ROM
    e000-e7ff RAM
    f000-f3ff Video RAM #1
    f400-f7ff Color RAM #1
    f800-fbff Video RAM #2
    fc00-ffff Color RAM #2

    I/O
    read:
    00  IN0
    01  IN1
    02  IN2
    04  DSW

    write:
    00  SN76496 #1
    01  SN76496 #2
    02  SN76496 #3
    05  horizontal scroll
    07  video control

***************************************************************************/

#include "emu.h"

#include "cpu/z80/z80.h"
#include "sound/sn76496.h"
#include "video/resnet.h"

#include "emupal.h"
#include "screen.h"
#include "speaker.h"
#include "tilemap.h"


namespace {

class bankp_state : public driver_device
{
public:
	bankp_state(machine_config const &mconfig, device_type type, char const *tag) :
		driver_device(mconfig, type, tag),
		m_videoram(*this, "videoram%u", 1U),
		m_colorram(*this, "colorram%u", 1U),
		m_maincpu(*this, "maincpu"),
		m_screen(*this, "screen"),
		m_gfxdecode(*this, "gfxdecode"),
		m_palette(*this, "palette"),
		m_sn76489(*this, "sn%u", 1U)
	{ }

	void bankp(machine_config &config);

protected:
	// initialization
	virtual void video_start() override;
	virtual void video_reset() override;

private:
	// memory pointers
	required_shared_ptr_array<u8, 2> m_videoram;
	required_shared_ptr_array<u8, 2> m_colorram;

	// devices
	required_device<cpu_device> m_maincpu;
	required_device<screen_device> m_screen;
	required_device<gfxdecode_device> m_gfxdecode;
	required_device<palette_device> m_palette;
	required_device_array<sn76489_device, 3> m_sn76489;

	// internal state
	u8 m_scroll_x = 0U;
	u8 m_priority = 0U;
	bool m_color_hi = false;
	bool m_display_on = false;
	bool m_nmi_mask = false;

	// video-related
	void scroll_w(u8 data);
	template <int const Which> void videoram_w(offs_t offset, u8 data);
	template <int const Which> void colorram_w(offs_t offset, u8 data);
	void video_control_w(u8 data);

	tilemap_t *m_bg_tilemap = nullptr;
	tilemap_t *m_fg_tilemap = nullptr;
	template <int const Which> TILE_GET_INFO_MEMBER(get_tile_info);

	void palette(palette_device &palette) const;

	u32 screen_update(screen_device &screen, bitmap_ind16 &bitmap, rectangle const &cliprect);

	void vblank_interrupt(int state);

	// address maps
	void io_map(address_map &map);
	void prg_map(address_map &map);
};


/***************************************************************************

  Convert the color PROMs into a more useable format.

  Bank Panic has a 32x8 palette PROM and two 256x4 lookup table PROMs (one
  for charset #1, one for charset #2 - only the first 128 nibbles are used).

  bit 7 -- 220 ohm resistor  -- BLUE
        -- 470 ohm resistor  -- BLUE
        -- 220 ohm resistor  -- GREEN
        -- 470 ohm resistor  -- GREEN
        -- 1  kohm resistor  -- GREEN
        -- 220 ohm resistor  -- RED
        -- 470 ohm resistor  -- RED
  bit 0 -- 1  kohm resistor  -- RED

***************************************************************************/

void bankp_state::palette(palette_device &palette) const
{
	u8 const *prom = memregion("proms")->base();
	static int constexpr resistances[3] = { 1000, 470, 220 };

	// compute the color output resistor weights
	double rweights[3], gweights[3], bweights[2];
	compute_resistor_weights(0, 255, -1.0,
		3, &resistances[0], rweights, 0, 0,
		3, &resistances[0], gweights, 0, 0,
		2, &resistances[1], bweights, 0, 0);

	// create a lookup table for the palette
	for (int i = 0; i < 0x20; i++)
	{
		bool bit0, bit1, bit2;

		// red component
		bit0 = BIT(prom[i], 0);
		bit1 = BIT(prom[i], 1);
		bit2 = BIT(prom[i], 2);
		u8 const r = combine_weights(rweights, bit0, bit1, bit2);

		// green component
		bit0 = BIT(prom[i], 3);
		bit1 = BIT(prom[i], 4);
		bit2 = BIT(prom[i], 5);
		u8 const g = combine_weights(gweights, bit0, bit1, bit2);

		// blue component
		bit0 = BIT(prom[i], 6);
		bit1 = BIT(prom[i], 7);
		u8 const b = combine_weights(bweights, bit0, bit1);

		palette.set_indirect_color(i, rgb_t(r, g, b));
	}

	// point to the beginning of the lookup table
	prom += 0x20;

	// foreground/background characters
	for (int i = 0; i < 0x100; i++)
	{
		// lookup tables are 256x4, but A7 is GND
		int index = (i << 1 & 0x100) | (i & 0x7f);

		palette.set_pen_indirect(index, prom[index] & 0xf);
		palette.set_pen_indirect(index | 0x80, (prom[index] & 0xf) | 0x10);
	}
}


/*************************************
 *
 *  Tilemaps
 *
 *************************************/

void bankp_state::scroll_w(u8 data)
{
	m_scroll_x = data;
}

template <int const Which>
void bankp_state::videoram_w(offs_t offset, u8 data)
{
	m_videoram[Which][offset] = data;
	(Which ? m_bg_tilemap : m_fg_tilemap)->mark_tile_dirty(offset);
}

template <int const Which>
void bankp_state::colorram_w(offs_t offset, u8 data)
{
	m_colorram[Which][offset] = data;
	(Which ? m_bg_tilemap : m_fg_tilemap)->mark_tile_dirty(offset);
}

void bankp_state::video_control_w(u8 data)
{
	// bits 0-1 are playfield priority
	// TODO: understand how this works
	m_priority = data & 0x03;

	// bit 2 turns on display
	m_display_on = BIT(data, 2);

	// bit 3 controls color prom d4
	if (m_color_hi != BIT(data, 3))
	{
		m_color_hi = BIT(data, 3);
		machine().tilemap().mark_all_dirty();
	}

	// bit 4 enables vblank NMI
	m_nmi_mask = BIT(data, 4);

	// bit 5 controls screen flip
	flip_screen_set(BIT(data, 5));

	// bits 6-7 N/C
}

template <int const Which>
TILE_GET_INFO_MEMBER(bankp_state::get_tile_info)
{
	int const Width  = Which ? 3 : 2;
	u8 const attr    = m_colorram[Which][tile_index];
	u8 const bank    = attr & make_bitmask<u8>(Width);
	bool const flipx = BIT(attr, Width);
	tileinfo.group   = (attr >> (Width + 1)) & make_bitmask<u8>(7 - Width);
	u8 const color   = tileinfo.group | (m_color_hi << (7 - Width));
	u16 const code   = m_videoram[Which][tile_index] | (bank << 8);
	int const flags  = flipx ? TILE_FLIPX : 0;

	tileinfo.set(Which, code, color, flags);
}

void bankp_state::video_start()
{
	save_item(NAME(m_scroll_x));
	save_item(NAME(m_priority));
	save_item(NAME(m_color_hi));
	save_item(NAME(m_display_on));
	save_item(NAME(m_nmi_mask));

	m_fg_tilemap = &machine().tilemap().create(*m_gfxdecode, tilemap_get_info_delegate(*this, FUNC(bankp_state::get_tile_info<0>)), TILEMAP_SCAN_ROWS, 8, 8, 32, 32);
	m_bg_tilemap = &machine().tilemap().create(*m_gfxdecode, tilemap_get_info_delegate(*this, FUNC(bankp_state::get_tile_info<1>)), TILEMAP_SCAN_ROWS, 8, 8, 32, 32);

	m_fg_tilemap->configure_groups(*m_gfxdecode->gfx(0), 0);
	m_bg_tilemap->configure_groups(*m_gfxdecode->gfx(1), 0);
}

void bankp_state::video_reset()
{
	video_control_w(0);
}


/*************************************
 *
 *  Screen update
 *
 *************************************/

u32 bankp_state::screen_update(screen_device &screen, bitmap_ind16 &bitmap, rectangle const &cliprect)
{
	if (!m_display_on)
	{
		bitmap.fill(m_palette->black_pen(), cliprect);
		return 0;
	}

	if (flip_screen())
	{
		m_fg_tilemap->set_scrollx(0, 240 - m_scroll_x);
		m_bg_tilemap->set_scrollx(0, 240);
	}
	else
	{
		m_fg_tilemap->set_scrollx(0, m_scroll_x);
		m_bg_tilemap->set_scrollx(0, 0);
	}

	// only one bit matters?
	switch (m_priority)
	{
	case 0: // combat hawk uses this
		m_bg_tilemap->draw(screen, bitmap, cliprect, TILEMAP_DRAW_OPAQUE);
		m_fg_tilemap->draw(screen, bitmap, cliprect);
		break;
	case 1:
		m_bg_tilemap->draw(screen, bitmap, cliprect, TILEMAP_DRAW_OPAQUE);
		m_fg_tilemap->draw(screen, bitmap, cliprect);
		break;
	case 2:
		m_fg_tilemap->draw(screen, bitmap, cliprect, TILEMAP_DRAW_OPAQUE);
		m_bg_tilemap->draw(screen, bitmap, cliprect);
		break;
	case 3:
		m_fg_tilemap->draw(screen, bitmap, cliprect, TILEMAP_DRAW_OPAQUE); // just a guess
		m_bg_tilemap->draw(screen, bitmap, cliprect);
		break;
	}

	return 0;
}


/*************************************
 *
 *  Address maps
 *
 *************************************/

void bankp_state::prg_map(address_map &map)
{
	map(0x0000, 0xdfff).rom();
	map(0xe000, 0xefff).ram();
	map(0xf000, 0xf3ff).ram().w(FUNC(bankp_state::videoram_w<0>)).share(m_videoram[0]);
	map(0xf400, 0xf7ff).ram().w(FUNC(bankp_state::colorram_w<0>)).share(m_colorram[0]);
	map(0xf800, 0xfbff).ram().w(FUNC(bankp_state::videoram_w<1>)).share(m_videoram[1]);
	map(0xfc00, 0xffff).ram().w(FUNC(bankp_state::colorram_w<1>)).share(m_colorram[1]);
}

void bankp_state::io_map(address_map &map)
{
	map.global_mask(0xff);

	map(0x00, 0x00).portr("IN0").w(m_sn76489[0], FUNC(sn76489_device::write));
	map(0x01, 0x01).portr("IN1").w(m_sn76489[1], FUNC(sn76489_device::write));
	map(0x02, 0x02).portr("IN2").w(m_sn76489[2], FUNC(sn76489_device::write));
	map(0x04, 0x04).portr("DSW1");
	map(0x05, 0x05).w(FUNC(bankp_state::scroll_w));
	map(0x07, 0x07).w(FUNC(bankp_state::video_control_w));
}


/*************************************
 *
 *  Input ports
 *
 *************************************/

static INPUT_PORTS_START( bankp )
	PORT_START("IN0")
	PORT_BIT( 0x01, IP_ACTIVE_HIGH, IPT_UNUSED )
	PORT_BIT( 0x02, IP_ACTIVE_HIGH, IPT_JOYSTICK_RIGHT ) PORT_2WAY
	PORT_BIT( 0x04, IP_ACTIVE_HIGH, IPT_UNUSED )
	PORT_BIT( 0x08, IP_ACTIVE_HIGH, IPT_JOYSTICK_LEFT )  PORT_2WAY
	PORT_BIT( 0x10, IP_ACTIVE_HIGH, IPT_BUTTON1 )
	PORT_BIT( 0x20, IP_ACTIVE_HIGH, IPT_COIN1 )
	PORT_BIT( 0x40, IP_ACTIVE_HIGH, IPT_SERVICE1 )
	PORT_BIT( 0x80, IP_ACTIVE_HIGH, IPT_BUTTON2 )

	PORT_START("IN1")
	PORT_BIT( 0x01, IP_ACTIVE_HIGH, IPT_UNUSED )
	PORT_BIT( 0x02, IP_ACTIVE_HIGH, IPT_JOYSTICK_RIGHT ) PORT_2WAY PORT_COCKTAIL
	PORT_BIT( 0x04, IP_ACTIVE_HIGH, IPT_UNUSED )
	PORT_BIT( 0x08, IP_ACTIVE_HIGH, IPT_JOYSTICK_LEFT )  PORT_2WAY PORT_COCKTAIL
	PORT_BIT( 0x10, IP_ACTIVE_HIGH, IPT_BUTTON1 )                  PORT_COCKTAIL
	PORT_BIT( 0x20, IP_ACTIVE_HIGH, IPT_START1 )
	PORT_BIT( 0x40, IP_ACTIVE_HIGH, IPT_START2 )
	PORT_BIT( 0x80, IP_ACTIVE_HIGH, IPT_BUTTON2 )                  PORT_COCKTAIL

	PORT_START("IN2")
	PORT_BIT( 0x01, IP_ACTIVE_HIGH, IPT_BUTTON3 )
	PORT_BIT( 0x02, IP_ACTIVE_HIGH, IPT_BUTTON3 ) PORT_COCKTAIL
	PORT_BIT( 0x04, IP_ACTIVE_HIGH, IPT_COIN2 )
	PORT_BIT( 0xf8, IP_ACTIVE_HIGH, IPT_UNUSED )

	PORT_START("DSW1")
	PORT_DIPNAME( 0x03, 0x00, "Coin Switch 1" )        PORT_DIPLOCATION("SW1:1,2")
	PORT_DIPSETTING(    0x03, DEF_STR( 3C_1C ) )
	PORT_DIPSETTING(    0x02, DEF_STR( 2C_1C ) )
	PORT_DIPSETTING(    0x00, DEF_STR( 1C_1C ) )
	PORT_DIPSETTING(    0x01, DEF_STR( 1C_2C ) )
	PORT_DIPNAME( 0x04, 0x00, "Coin Switch 2" )        PORT_DIPLOCATION("SW1:3")
	PORT_DIPSETTING(    0x04, DEF_STR( 2C_1C ) )
	PORT_DIPSETTING(    0x00, DEF_STR( 1C_1C ) )
	PORT_DIPNAME( 0x08, 0x00, DEF_STR( Lives ) )       PORT_DIPLOCATION("SW1:4")
	PORT_DIPSETTING(    0x00, "3" )
	PORT_DIPSETTING(    0x08, "4" )
	PORT_DIPNAME( 0x10, 0x00, DEF_STR( Bonus_Life ) )  PORT_DIPLOCATION("SW1:5")
	PORT_DIPSETTING(    0x00, "70K 200K 500K" )        // and 900K 1500K 2000K
	PORT_DIPSETTING(    0x10, "100K 400K 800K" )       // and 1200K 2000K 3000K
	PORT_DIPNAME( 0x20, 0x00, DEF_STR( Difficulty ) )  PORT_DIPLOCATION("SW1:6")
	PORT_DIPSETTING(    0x00, DEF_STR( Easy ) )
	PORT_DIPSETTING(    0x20, DEF_STR( Hard ) )
	PORT_DIPNAME( 0x40, 0x40, DEF_STR( Demo_Sounds ) ) PORT_DIPLOCATION("SW1:7")
	PORT_DIPSETTING(    0x00, DEF_STR( Off ) )
	PORT_DIPSETTING(    0x40, DEF_STR( On ) )
	PORT_DIPNAME( 0x80, 0x80, DEF_STR( Cabinet ) )     PORT_DIPLOCATION("SW1:8")
	PORT_DIPSETTING(    0x80, DEF_STR( Upright ) )
	PORT_DIPSETTING(    0x00, DEF_STR( Cocktail ) )
INPUT_PORTS_END

static INPUT_PORTS_START( combh )
	PORT_INCLUDE( bankp )

	PORT_MODIFY("IN0")
	PORT_BIT( 0x01, IP_ACTIVE_HIGH, IPT_JOYSTICK_UP )   PORT_2WAY
	PORT_BIT( 0x02, IP_ACTIVE_HIGH, IPT_UNUSED )
	PORT_BIT( 0x04, IP_ACTIVE_HIGH, IPT_JOYSTICK_DOWN ) PORT_2WAY
	PORT_BIT( 0x08, IP_ACTIVE_HIGH, IPT_UNUSED )

	PORT_MODIFY("IN1")
	PORT_BIT( 0x01, IP_ACTIVE_HIGH, IPT_JOYSTICK_UP )   PORT_2WAY PORT_COCKTAIL
	PORT_BIT( 0x02, IP_ACTIVE_HIGH, IPT_UNUSED )
	PORT_BIT( 0x04, IP_ACTIVE_HIGH, IPT_JOYSTICK_DOWN ) PORT_2WAY PORT_COCKTAIL
	PORT_BIT( 0x08, IP_ACTIVE_HIGH, IPT_UNUSED )

	PORT_MODIFY("DSW1")
	PORT_DIPNAME( 0x01, 0x00, DEF_STR( Flip_Screen ) )     PORT_DIPLOCATION("SW1:1")
	PORT_DIPSETTING(    0x00, DEF_STR( Off ) )
	PORT_DIPSETTING(    0x01, DEF_STR( On ) )
	PORT_DIPNAME( 0x06, 0x00, DEF_STR( Coinage ) )         PORT_DIPLOCATION("SW1:2,3")
	PORT_DIPSETTING(    0x06, DEF_STR( 2C_1C ) )
	PORT_DIPSETTING(    0x00, DEF_STR( 1C_1C ) )
	PORT_DIPSETTING(    0x02, DEF_STR( 1C_2C ) )
	PORT_DIPSETTING(    0x04, DEF_STR( 1C_3C ) )
	PORT_DIPNAME( 0x10, 0x10, DEF_STR( Cabinet ) )         PORT_DIPLOCATION("SW1:5")
	PORT_DIPSETTING(    0x10, DEF_STR( Upright ) )
	PORT_DIPSETTING(    0x00, DEF_STR( Cocktail ) )
	PORT_DIPUNUSED_DIPLOC( 0x20, IP_ACTIVE_HIGH, "SW1:6" )
	PORT_DIPNAME( 0x40, 0x00, DEF_STR( Difficulty ) )      PORT_DIPLOCATION("SW1:7")
	PORT_DIPSETTING(    0x00, DEF_STR( Easy ) )
	PORT_DIPSETTING(    0x40, DEF_STR( Hard ) )
	PORT_DIPNAME( 0x80, 0x00, "Fuel" )                     PORT_DIPLOCATION("SW1:8")
	PORT_DIPSETTING(    0x00, "120 Units" )
	PORT_DIPSETTING(    0x80, "90 Units" )
INPUT_PORTS_END


/*************************************
 *
 *  Graphics definitions
 *
 *************************************/

static gfx_layout const gfx_8x8x2 =
{
	8,8,
	RGN_FRAC(1,1),
	2,
	{ 0, 4 },
	{ STEP4(8*8+3,-1), STEP4(0*8+3,-1) },
	{ STEP8(0*8,8) },
	16*8
};

static gfx_layout const gfx_8x8x3 =
{
	8,8,
	RGN_FRAC(1,3),
	3,
	{ RGN_FRAC(0,3), RGN_FRAC(1,3), RGN_FRAC(2,3) },
	{ STEP8(7,-1) },
	{ STEP8(0*8,8) },
	8*8
};

static GFXDECODE_START( gfx_bankp )
	GFXDECODE_ENTRY( "fgtiles", 0, gfx_8x8x2, 0,    64 )
	GFXDECODE_ENTRY( "bgtiles", 0, gfx_8x8x3, 64*4, 32 )
GFXDECODE_END



/*************************************
 *
 *  Machine driver
 *
 *************************************/

void bankp_state::vblank_interrupt(int state)
{
	if (state && m_nmi_mask)
		m_maincpu->pulse_input_line(INPUT_LINE_NMI, attotime::zero);
}

void bankp_state::bankp(machine_config &config)
{
	XTAL constexpr MASTER_CLOCK = XTAL(15'468'480);

	// basic machine hardware
	Z80(config, m_maincpu, MASTER_CLOCK / 6);
	m_maincpu->set_addrmap(AS_PROGRAM, &bankp_state::prg_map);
	m_maincpu->set_addrmap(AS_IO, &bankp_state::io_map);

	// video hardware
	SCREEN(config, m_screen, SCREEN_TYPE_RASTER);
	// PCB measured: H = 15.61khz V = 60.99hz, +/- 0.01hz
	// --> VTOTAL should be OK, HTOTAL not 100% certain
	m_screen->set_raw(MASTER_CLOCK / 3, 330, 0 + 3 * 8, 224 + 3 * 8, 256, 0 + 2 * 8, 224 + 2 * 8);
	m_screen->set_screen_update(FUNC(bankp_state::screen_update));
	m_screen->screen_vblank().set(FUNC(bankp_state::vblank_interrupt));
	m_screen->set_palette(m_palette);

	GFXDECODE(config, m_gfxdecode, m_palette, gfx_bankp);
	PALETTE(config, m_palette, FUNC(bankp_state::palette), 512, 32);

	// sound hardware
	SPEAKER(config, "mono").front_center();

	SN76489(config, m_sn76489[0], MASTER_CLOCK / 6).add_route(ALL_OUTPUTS, "mono", 1.0);
	SN76489(config, m_sn76489[1], MASTER_CLOCK / 6).add_route(ALL_OUTPUTS, "mono", 1.0);
	SN76489(config, m_sn76489[2], MASTER_CLOCK / 6).add_route(ALL_OUTPUTS, "mono", 1.0);
}


/*************************************
 *
 *  ROM definition(s)
 *
 *************************************/

ROM_START( bankp )
	ROM_REGION( 0xe000, "maincpu", 0 )
	ROM_LOAD( "epr-6175.7e",      0x0000, 0x4000, CRC(044552b8) SHA1(8d50ba062483d4789cfd3ed86cea53dff0ff6968) )
	ROM_LOAD( "epr-6174.7f",      0x4000, 0x4000, CRC(d29b1598) SHA1(8c1ee4d23d8d6f93af3e22f2cba189b0055994fb) )
	ROM_LOAD( "epr-6173.7h",      0x8000, 0x4000, CRC(b8405d38) SHA1(0f62a972f38b4ddcea77eb0e1d76c70ddbcb7b11) )
	ROM_LOAD( "epr-6176.7d",      0xc000, 0x2000, CRC(c98ac200) SHA1(1bdb87868deebe03da18280e617530c24118da1c) )

	ROM_REGION( 0x4000, "fgtiles", 0 )
	ROM_LOAD( "epr-6165.5l",      0x0000, 0x2000, CRC(aef34a93) SHA1(513895cd3144977b3d9b5ac7f2bf40384d69e157) )
	ROM_LOAD( "epr-6166.5k",      0x2000, 0x2000, CRC(ca13cb11) SHA1(3aca0b0d3f052a742e1cd0b96bfad834e78fcd7d) )

	ROM_REGION( 0xc000, "bgtiles", 0 )
	ROM_LOAD( "epr-6172.5b",      0x0000, 0x2000, CRC(c4c4878b) SHA1(423143d81408eda96f87bdc3a306517c473cbe00) )
	ROM_LOAD( "epr-6171.5d",      0x2000, 0x2000, CRC(a18165a1) SHA1(9a7513ea84f9231edba4e637df28a1705c8cdeb0) )
	ROM_LOAD( "epr-6170.5e",      0x4000, 0x2000, CRC(b58aa8fa) SHA1(432b43cd9af4e3dab579cfd191b731aa11ceb121) )
	ROM_LOAD( "epr-6169.5f",      0x6000, 0x2000, CRC(1aa37fce) SHA1(6e2402683145de8972a53c9ec01da9a422392bed) )
	ROM_LOAD( "epr-6168.5h",      0x8000, 0x2000, CRC(05f3a867) SHA1(9da11c3cea967c5f0d7397c0ff4f87b4b1446c4c) )
	ROM_LOAD( "epr-6167.5i",      0xa000, 0x2000, CRC(3fa337e1) SHA1(5fdc45436be27cceb5157bd6201c30e3de28fd7b) )

	ROM_REGION( 0x0220, "proms", 0 )
	ROM_LOAD( "pr-6177.8a",       0x0000, 0x020, CRC(eb70c5ae) SHA1(13613dad6c14004278f777d6f3f62712a2a85773) ) // palette
	ROM_LOAD( "pr-6178.6f",       0x0020, 0x100, CRC(0acca001) SHA1(54c354d825a24a9085867b114a2cd6835baebe55) ) // fgtiles lookup table
	ROM_LOAD( "pr-6179.5a",       0x0120, 0x100, CRC(e53bafdb) SHA1(7a414f6db5476dd7d0217e5b846ed931381eda02) ) // bgtiles lookup table

	ROM_REGION( 0x025c, "user1", 0 )
	ROM_LOAD( "315-5074.2c.bin",  0x0000, 0x025b, CRC(2e57bbba) SHA1(c3e45e8a972342779442e50872a2f5f2d61e9c0a) )
	ROM_LOAD( "315-5073.pal16l4", 0x0000, 0x0001, NO_DUMP ) // read protected
ROM_END

ROM_START( bankpt )
	ROM_REGION( 0xe000, "maincpu", 0 )
	ROM_LOAD( "11.bin",       0x0000, 0x4000, CRC(056755ce) SHA1(7375b856a5757b8b6336ccc16df460a79c3f25a7) )
	ROM_LOAD( "10.bin",       0x4000, 0x4000, CRC(d29b1598) SHA1(8c1ee4d23d8d6f93af3e22f2cba189b0055994fb) )
	ROM_LOAD( "9.bin",        0x8000, 0x4000, CRC(08a8137b) SHA1(1dbda68825101d5474eecc58ff9b5f9fe79b0b33) )
	ROM_LOAD( "12.bin",       0xc000, 0x2000, CRC(c98ac200) SHA1(1bdb87868deebe03da18280e617530c24118da1c) )

	ROM_REGION( 0x4000, "fgtiles", 0 )
	ROM_LOAD( "1.bin",        0x0000, 0x2000, CRC(aef34a93) SHA1(513895cd3144977b3d9b5ac7f2bf40384d69e157) )
	ROM_LOAD( "2.bin",        0x2000, 0x2000, CRC(ca13cb11) SHA1(3aca0b0d3f052a742e1cd0b96bfad834e78fcd7d) )

	ROM_REGION( 0xc000, "bgtiles", 0 )
	ROM_LOAD( "8.bin",        0x0000, 0x2000, CRC(c4c4878b) SHA1(423143d81408eda96f87bdc3a306517c473cbe00) )
	ROM_LOAD( "7.bin",        0x2000, 0x2000, CRC(a18165a1) SHA1(9a7513ea84f9231edba4e637df28a1705c8cdeb0) )
	ROM_LOAD( "6.bin",        0x4000, 0x2000, CRC(b58aa8fa) SHA1(432b43cd9af4e3dab579cfd191b731aa11ceb121) )
	ROM_LOAD( "5.bin",        0x6000, 0x2000, CRC(1aa37fce) SHA1(6e2402683145de8972a53c9ec01da9a422392bed) )
	ROM_LOAD( "4.bin",        0x8000, 0x2000, CRC(05f3a867) SHA1(9da11c3cea967c5f0d7397c0ff4f87b4b1446c4c) )
	ROM_LOAD( "3.bin",        0xa000, 0x2000, CRC(3fa337e1) SHA1(5fdc45436be27cceb5157bd6201c30e3de28fd7b) )

	ROM_REGION( 0x0220, "proms", 0 )
	ROM_LOAD( "82s123.bin",   0x0000, 0x020, CRC(eb70c5ae) SHA1(13613dad6c14004278f777d6f3f62712a2a85773) ) // palette
	ROM_LOAD( "6_82s129.bin", 0x0020, 0x100, CRC(0acca001) SHA1(54c354d825a24a9085867b114a2cd6835baebe55) ) // fgtiles lookup table
	ROM_LOAD( "5_82s129.bin", 0x0120, 0x100, CRC(e53bafdb) SHA1(7a414f6db5476dd7d0217e5b846ed931381eda02) ) // bgtiles lookup table

	ROM_REGION( 0x035f, "user1", 0 )
	ROM_LOAD( "82s153.bin",   0x0000, 0x025b, NO_DUMP ) // read protected
	ROM_LOAD( "pal16r4.bin",  0x0000, 0x0104, NO_DUMP ) // read protected
ROM_END

ROM_START( combh )
	ROM_REGION( 0xe000, "maincpu", 0 )
	ROM_LOAD( "epr-10904.7e",      0x0000, 0x4000, CRC(4b106335) SHA1(1cdfac301b52ccd98d09b52089bb2a45fc9afdbb) )
	ROM_LOAD( "epr-10905.7f",      0x4000, 0x4000, CRC(a76fc390) SHA1(9ffc453010ffb93db5f549e3cc1e0a4eb39ac61c) )
	ROM_LOAD( "epr-10906.7h",      0x8000, 0x4000, CRC(16d54885) SHA1(e00fc618bb1a1f8c160fe2a0d4de6d9750313643) )
	ROM_LOAD( "epr-10903.7d",      0xc000, 0x2000, CRC(b7a59cab) SHA1(6321fa9bdf580d76267b13fcf7dc066a45e0c926) )

	ROM_REGION( 0x4000, "fgtiles", 0 )
	ROM_LOAD( "epr-10914.5l",      0x0000, 0x2000, CRC(7d7a2340) SHA1(7411131bf929eaf11e12047eea74158daf8dd274) )
	ROM_LOAD( "epr-10913.5k",      0x2000, 0x2000, CRC(d5c1a8ae) SHA1(4f05fd183918bcc5e7b312df5a22f92756cec01d) )

	ROM_REGION( 0xc000, "bgtiles", 0 )
	ROM_LOAD( "epr-10907.5b",      0x0000, 0x2000, CRC(08e5eea3) SHA1(1ab46f9f390a4d8647b0bc07bff66f4d7f47b19e) )
	ROM_LOAD( "epr-10908.5d",      0x2000, 0x2000, CRC(d9e413f5) SHA1(c9eb038eed97fcdb56c368f5540b372a1c7b4250) )
	ROM_LOAD( "epr-10909.5e",      0x4000, 0x2000, CRC(fec7962c) SHA1(1e58cb19d3a80164a9968e42eea0503364dad017) )
	ROM_LOAD( "epr-10910.5f",      0x6000, 0x2000, CRC(33db0fa7) SHA1(51f73a216d1349fd194bcc166bfbc7a39935f1bf) )
	ROM_LOAD( "epr-10911.5h",      0x8000, 0x2000, CRC(565d9e6d) SHA1(99071eaacfc571eb55e199f2f723fe6ef109b07a) )
	ROM_LOAD( "epr-10912.5i",      0xa000, 0x2000, CRC(cbe22738) SHA1(2dbdb593882ec66e783411f02941ce822e1c62a1) )

	ROM_REGION( 0x0220, "proms", 0 )
	ROM_LOAD( "pr-10900.8a",       0x0000, 0x020, CRC(f95fcd66) SHA1(ed7bf6691a942f344b0230310876a63a68606922) ) // palette
	ROM_LOAD( "pr-10901.6f",       0x0020, 0x100, CRC(6fd981c8) SHA1(0bd2e7b72fd5e055224a675108e2e706cd6f6e5a) ) // fgtiles lookup table
	ROM_LOAD( "pr-10902.5a",       0x0120, 0x100, CRC(84d6bded) SHA1(67d9c4c7d7c84eb54ec655a4cf1768ca0cbb047d) ) // bgtiles lookup table

	ROM_REGION( 0x025c, "user1", 0 )
	ROM_LOAD( "315-5074.2c.bin",   0x0000, 0x025b, CRC(2e57bbba) SHA1(c3e45e8a972342779442e50872a2f5f2d61e9c0a) )
	ROM_LOAD( "315-5073.pal16l4",  0x0000, 0x0001, NO_DUMP ) // read protected
ROM_END

} // anonymous namespace


/*************************************
 *
 *  Game driver(s)
 *
 *************************************/

GAME( 1984, bankp,  0,     bankp, bankp, bankp_state, empty_init, ROT0,   "Sanritsu / Sega",  "Bank Panic",                  MACHINE_SUPPORTS_SAVE )
GAME( 1984, bankpt, bankp, bankp, bankp, bankp_state, empty_init, ROT0,   "bootleg (Tecfri)", "Bank Panic (Tecfri bootleg)", MACHINE_SUPPORTS_SAVE )
GAME( 1987, combh,  0,     bankp, combh, bankp_state, empty_init, ROT270, "Sanritsu / Sega",  "Combat Hawk",                 MACHINE_SUPPORTS_SAVE )
