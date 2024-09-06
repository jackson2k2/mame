// license:BSD-3-Clause
// copyright-holders:Takahiro Nogi
/***************************************************************************

    Mr. Jong (ミスタージャン)
    (c)1983 Kiwako (This game is distributed by Sanritsu.)

    Crazy Blocks
    (c)1983 Kiwako / ECI

    Block Buster
    (c)1983 Kiwako / ECI

    Driver by Takahiro Nogi 2000/03/20 -

    Hardware seems to be more similar to Bank Panic than Dr. Micro
    and Appoooh (which are more akin to Galaxian hardware designs)

    TODO: mirrors, verify palette, params, invincibility DSW?


PCB Layout
----------


C2-00154C
|-----------------------------------------|
|                    93422                |
|                    93422                |
|          4H  5H           PROM7J        |
|                           PAL    DSW1(8)|
|              PROM5G                     |
|                           76489         |
|                           76489         |
|               Z80                       |
|                                PAL      |
|15.468MHz PAL              6116     555  |
|                                         |
|         6116          6A 7A 8A 9A  6116 |
|                                         |
|-----------------------------------------|

Notes:
          Z80 clock: 2.576MHz (= XTAL / 6)
      XTAL measured: 15.459MHz
             PROM5G: MB7052 = 82S129
             PROM7J: MB7056 = 82S123
     ROMs 4H and 5h: 2732
ROMs 6A, 7A, 8A, 9A: 2764

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

class mrjong_state : public driver_device
{
public:
	mrjong_state(machine_config const &mconfig, device_type type, char const *tag) :
		driver_device(mconfig, type, tag),
		m_videoram(*this, "videoram"),
		m_colorram(*this, "colorram"),
		m_maincpu(*this, "maincpu"),
		m_screen(*this, "screen"),
		m_gfxdecode(*this, "gfxdecode"),
		m_palette(*this, "palette"),
		m_sn76489(*this, "sn%u", 1U)
	{ }

	void mrjong(machine_config &config);

protected:
	// initialization
	virtual void video_start() override;

private:
	// memory pointers
	required_shared_ptr<u8> m_videoram;
	required_shared_ptr<u8> m_colorram;

	// devices
	required_device<cpu_device> m_maincpu;
	required_device<screen_device> m_screen;
	required_device<gfxdecode_device> m_gfxdecode;
	required_device<palette_device> m_palette;
	required_device_array<sn76489_device, 2> m_sn76489;

	// video
	void videoram_w(offs_t offset, u8 data);
	void colorram_w(offs_t offset, u8 data);

	void flipscreen_w(u8 data);

	void palette(palette_device &palette) const;

	tilemap_t *m_bg_tilemap = nullptr;
	TILE_GET_INFO_MEMBER(get_bg_tile_info);

	void draw_sprites(bitmap_ind16 &bitmap, rectangle const &cliprect);

	u32 screen_update(screen_device &screen, bitmap_ind16 &bitmap, rectangle const &cliprect);

	// address maps
	void program_map(address_map &map);
	void io_map(address_map &map);
};


/***************************************************************************

  Convert the color PROMs.

***************************************************************************/

void mrjong_state::palette(palette_device &palette) const
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
	for (int i = 0; i < 0x10; i++)
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

	// characters/sprites
	for (int i = 0; i < 0x80; i++)
	{
		u8 const ctabentry = prom[i] & 0x0f;
		palette.set_pen_indirect(i, ctabentry);
	}
}


/***************************************************************************

  Display control parameter.

***************************************************************************/

void mrjong_state::videoram_w(offs_t offset, u8 data)
{
	if (m_videoram[offset] != data)
	{
		m_videoram[offset] = data;
		m_bg_tilemap->mark_tile_dirty(offset);
	}
}

void mrjong_state::colorram_w(offs_t offset, u8 data)
{
	if (m_colorram[offset] != data)
	{
		m_colorram[offset] = data;
		m_bg_tilemap->mark_tile_dirty(offset);
	}
}

void mrjong_state::flipscreen_w(u8 data)
{
	// are there any other bits?
	if (flip_screen() != BIT(data, 2))
	{
		flip_screen_set(BIT(data, 2));
		machine().tilemap().mark_all_dirty();
	}
}

TILE_GET_INFO_MEMBER(mrjong_state::get_bg_tile_info)
{
	u8 const attr   = m_colorram[tile_index];
	bool const bank = BIT(attr, 5);
	u8 const color  = attr & 0x1f;
	u16 const code  = m_videoram[tile_index] | (bank << 8);
	int const flags = (BIT(attr, 6) ? TILE_FLIPX : 0) | (BIT(attr, 7) ? TILE_FLIPY : 0);

	tileinfo.set(0, code, color, flags);
}

void mrjong_state::video_start()
{
	m_bg_tilemap = &machine().tilemap().create(*m_gfxdecode, tilemap_get_info_delegate(*this, FUNC(mrjong_state::get_bg_tile_info)), TILEMAP_SCAN_ROWS_FLIP_XY, 8, 8, 32, 32);
}

void mrjong_state::draw_sprites(bitmap_ind16 &bitmap, rectangle const &cliprect)
{
	// NOTE: First 0x40 entries in the video RAM are actually sprite RAM
	for (int offs = 0x40 - 4; offs >= 0; offs -= 4)
	{
		u8 const *src   = &m_videoram[offs];
		int ypos        = src[0];
		int xpos        = 224 - src[2];
		bool flipx      = BIT(src[1], 0);
		bool flipy      = BIT(src[1], 1);
		u8 const color  = src[3] & 0x1f;
		bool const bank = BIT(src[3], 5);
		u8 const code   = ((src[1] >> 2) & 0x3f) | (bank << 6);

		if (flip_screen())
		{
			xpos = 192 - xpos;
			ypos = 240 - ypos;
			flipx = !flipx;
			flipy = !flipy;
		}

		m_gfxdecode->gfx(1)->transpen(bitmap, cliprect, code, color, flipx, flipy, xpos, ypos, 0);
	}
}

u32 mrjong_state::screen_update(screen_device &screen, bitmap_ind16 &bitmap, rectangle const &cliprect)
{
	m_bg_tilemap->draw(screen, bitmap, cliprect, 0, 0);
	draw_sprites(bitmap, cliprect);
	return 0;
}


/*************************************
 *
 *  Address maps
 *
 *************************************/

void mrjong_state::program_map(address_map &map)
{
	map(0x0000, 0x7fff).rom();
	map(0x8000, 0x87ff).ram();
	map(0xa000, 0xa7ff).ram();
	map(0xe000, 0xe3ff).ram().w(FUNC(mrjong_state::videoram_w)).share(m_videoram);
	map(0xe400, 0xe7ff).ram().w(FUNC(mrjong_state::colorram_w)).share(m_colorram);
}

void mrjong_state::io_map(address_map &map)
{
	map.global_mask(0xff);

	map(0x00, 0x00).portr("P2").w(FUNC(mrjong_state::flipscreen_w));
	map(0x01, 0x01).portr("P1").w(m_sn76489[0], FUNC(sn76489_device::write));
	map(0x02, 0x02).portr("DSW").w(m_sn76489[1], FUNC(sn76489_device::write));
	map(0x03, 0x03).portr("UNK");
}

/*************************************
 *
 *  Input ports
 *
 *************************************/

static INPUT_PORTS_START( mrjong )
	PORT_START("P1")
	PORT_BIT( 0x01, IP_ACTIVE_HIGH, IPT_JOYSTICK_UP )    PORT_4WAY PORT_PLAYER(1)
	PORT_BIT( 0x02, IP_ACTIVE_HIGH, IPT_JOYSTICK_LEFT )  PORT_4WAY PORT_PLAYER(1)
	PORT_BIT( 0x04, IP_ACTIVE_HIGH, IPT_JOYSTICK_RIGHT ) PORT_4WAY PORT_PLAYER(1)
	PORT_BIT( 0x08, IP_ACTIVE_HIGH, IPT_JOYSTICK_DOWN )  PORT_4WAY PORT_PLAYER(1)
	PORT_BIT( 0x10, IP_ACTIVE_HIGH, IPT_BUTTON1 )        PORT_PLAYER(1)
	PORT_BIT( 0x20, IP_ACTIVE_HIGH, IPT_COIN1 )
	PORT_BIT( 0x40, IP_ACTIVE_HIGH, IPT_COIN2 )
	PORT_BIT( 0x80, IP_ACTIVE_HIGH, IPT_UNKNOWN )

	PORT_START("P2")
	PORT_BIT( 0x01, IP_ACTIVE_HIGH, IPT_JOYSTICK_UP )    PORT_4WAY PORT_PLAYER(2)
	PORT_BIT( 0x02, IP_ACTIVE_HIGH, IPT_JOYSTICK_LEFT )  PORT_4WAY PORT_PLAYER(2)
	PORT_BIT( 0x04, IP_ACTIVE_HIGH, IPT_JOYSTICK_RIGHT ) PORT_4WAY PORT_PLAYER(2)
	PORT_BIT( 0x08, IP_ACTIVE_HIGH, IPT_JOYSTICK_DOWN )  PORT_4WAY PORT_PLAYER(2)
	PORT_BIT( 0x10, IP_ACTIVE_HIGH, IPT_BUTTON1 )        PORT_PLAYER(2)
	PORT_BIT( 0x20, IP_ACTIVE_HIGH, IPT_START1 )
	PORT_BIT( 0x40, IP_ACTIVE_HIGH, IPT_START2 )
	PORT_BIT( 0x80, IP_ACTIVE_LOW, IPT_UNKNOWN )         // ????

	PORT_START("DSW")
	PORT_DIPNAME( 0x01, 0x01, DEF_STR( Cabinet ) )
	PORT_DIPSETTING(    0x01, DEF_STR( Upright ) )
	PORT_DIPSETTING(    0x00, DEF_STR( Cocktail ) )
	PORT_DIPNAME( 0x02, 0x00, DEF_STR( Flip_Screen ) )
	PORT_DIPSETTING(    0x00, DEF_STR( Off ) )
	PORT_DIPSETTING(    0x02, DEF_STR( On ) )
	PORT_DIPNAME( 0x04, 0x00, DEF_STR( Bonus_Life ) )
	PORT_DIPSETTING(    0x00, "30k")
	PORT_DIPSETTING(    0x04, "50k")
	PORT_DIPNAME( 0x08, 0x00, DEF_STR( Difficulty ) )
	PORT_DIPSETTING(    0x00, DEF_STR( Normal ) )
	PORT_DIPSETTING(    0x08, DEF_STR( Hard ) )
	PORT_DIPNAME( 0x30, 0x00, DEF_STR( Lives ) )
	PORT_DIPSETTING(    0x00, "3")
	PORT_DIPSETTING(    0x10, "4")
	PORT_DIPSETTING(    0x20, "5")
	PORT_DIPSETTING(    0x30, "6")
	PORT_DIPNAME( 0xc0, 0x00, DEF_STR( Coinage ) )
	PORT_DIPSETTING(    0xc0, DEF_STR( 2C_1C ) )
	PORT_DIPSETTING(    0x00, DEF_STR( 1C_1C ) )
	PORT_DIPSETTING(    0x40, DEF_STR( 1C_2C ) )
	PORT_DIPSETTING(    0x80, DEF_STR( 1C_3C ) )

	PORT_START("UNK") // is this a jumper?
	PORT_DIPNAME( 0x01, 0x00, "Invincibility (Debug?)" )
	PORT_DIPSETTING(    0x01, DEF_STR( On ) )
	PORT_DIPSETTING(    0x00, DEF_STR( Off ) )
INPUT_PORTS_END


/*************************************
 *
 *  Graphics definitions
 *
 *************************************/

static const gfx_layout tilelayout =
{
	8, 8,
	RGN_FRAC(1,2),
	2,
	{ RGN_FRAC(0,2), RGN_FRAC(1,2) },
	{ STEP8(0,1) },
	{ STEP8(7*8,-8) },
	8 * 8
};

static const gfx_layout spritelayout =
{
	16, 16,
	RGN_FRAC(1,2),
	2,
	{ RGN_FRAC(0,2), RGN_FRAC(1,2) },
	{ STEP8(8*8,1), STEP8(0,1) },
	{ STEP8(23*8,-8), STEP8(7*8,-8) },
	32 * 8
};

static GFXDECODE_START( gfx_mrjong )
	GFXDECODE_ENTRY( "gfx", 0x0000, tilelayout,   0, 32 )
	GFXDECODE_ENTRY( "gfx", 0x0000, spritelayout, 0, 32 )
GFXDECODE_END


/*************************************
 *
 *  Machine driver
 *
 *************************************/

void mrjong_state::mrjong(machine_config &config)
{
	XTAL constexpr MASTER_CLOCK = 15.46848_MHz_XTAL;

	// basic machine hardware
	Z80(config, m_maincpu, MASTER_CLOCK / 6); // 2.578 MHz
	m_maincpu->set_addrmap(AS_PROGRAM, &mrjong_state::program_map);
	m_maincpu->set_addrmap(AS_IO, &mrjong_state::io_map);

	// video hardware
	SCREEN(config, m_screen, SCREEN_TYPE_RASTER);
	m_screen->set_raw(MASTER_CLOCK / 3, 320, 0, 240, 262, 16, 240); // hand adjusted for 61.5Hz
	m_screen->set_screen_update(FUNC(mrjong_state::screen_update));
	m_screen->set_palette(m_palette);
	m_screen->screen_vblank().set_inputline(m_maincpu, INPUT_LINE_NMI);

	GFXDECODE(config, m_gfxdecode, m_palette, gfx_mrjong);
	PALETTE(config, m_palette, FUNC(mrjong_state::palette), 4 * 32, 16);

	// sound hardware
	SPEAKER(config, "mono").front_center();

	SN76489(config, m_sn76489[0], MASTER_CLOCK / 6).add_route(ALL_OUTPUTS, "mono", 1.0);
	SN76489(config, m_sn76489[1], MASTER_CLOCK / 6).add_route(ALL_OUTPUTS, "mono", 1.0);
}


/*************************************
 *
 *  ROM definition(s)
 *
 *************************************/

ROM_START( mrjong )
	ROM_REGION( 0x8000, "maincpu", 0 )
	ROM_LOAD( "mj00", 0x0000, 0x2000, CRC(d211aed3) SHA1(01f252ca1d2399146fa3ed44cb2daa1d5925cae5) )
	ROM_LOAD( "mj01", 0x2000, 0x2000, CRC(49a9ca7e) SHA1(fc5279ba782da2c8288042bd17282366fcd788cc) )
	ROM_LOAD( "mj02", 0x4000, 0x2000, CRC(4b50ae6a) SHA1(6fa6bae926c5e4cc154f5f1a6dc7bb7ef5bb484a) )
	ROM_LOAD( "mj03", 0x6000, 0x2000, CRC(2c375a17) SHA1(9719485cdca535771b498a37d57734463858f2cd) )

	ROM_REGION( 0x2000, "gfx", 0 )
	ROM_LOAD( "mj21", 0x0000, 0x1000, CRC(1ea99dab) SHA1(21a296d394e5cac0c7cb2ea8efaeeeee976ac4b5) )
	ROM_LOAD( "mj20", 0x1000, 0x1000, CRC(7eb1d381) SHA1(fa13700f132c03d2d2cee65abf24024db656aff7) )

	ROM_REGION( 0x0120, "proms", 0 )
	ROM_LOAD( "mj61", 0x0000, 0x0020, CRC(a85e9b27) SHA1(55df208b771a98fcf6c2c19ffdf973891ebcabd1) )
	ROM_LOAD( "mj60", 0x0020, 0x0100, CRC(dd2b304f) SHA1(d7320521e83ddf269a9fc0c91f0e0e61428b187c) )
ROM_END

ROM_START( crazyblk )
	ROM_REGION( 0x8000, "maincpu", 0 )
	ROM_LOAD( "c1.a6",  0x0000, 0x2000, CRC(e2a211a2) SHA1(5bcf5a0cb25ce5adfb6519c8a3a4ee6e55e1e7de) )
	ROM_LOAD( "c2.a7",  0x2000, 0x2000, CRC(75070978) SHA1(7f59460c094e596a521014f956d76e5c714022a2) )
	ROM_LOAD( "c3.a7",  0x4000, 0x2000, CRC(696ca502) SHA1(8ce7e31e9a7161633fee7f28b215e4358d906c4b) )
	ROM_LOAD( "c4.a8",  0x6000, 0x2000, CRC(c7f5a247) SHA1(de79341f9c6c7032f76cead46d614e13d4af50f9) )

	ROM_REGION( 0x2000, "gfx", 0 )
	ROM_LOAD( "c6.h5",  0x0000, 0x1000, CRC(2b2af794) SHA1(d13bc8e8ea6c9bc2066ed692108151523d1f936b) )
	ROM_LOAD( "c5.h4",  0x1000, 0x1000, CRC(98d13915) SHA1(b51104f9f80128ff7a52ac2efa9519bf9d7b78bc) )

	ROM_REGION( 0x0120, "proms", 0 )
	ROM_LOAD( "clr.j7", 0x0000, 0x0020, CRC(ee1cf1d5) SHA1(4f4cfde1a896da92d8265889584dd0c5678de033) )
	ROM_LOAD( "clr.g5", 0x0020, 0x0100, CRC(bcb1e2e3) SHA1(c09731836a9d4e50316a84b86f61b599a1ef944d) )
ROM_END

ROM_START( blkbustr )
	ROM_REGION( 0x8000, "maincpu", 0 )
	ROM_LOAD( "6a.bin", 0x0000, 0x2000, CRC(9e4b426c) SHA1(831360c473ab2452f4d0da12609c96c601e21c17) )
	ROM_LOAD( "c2.a7",  0x2000, 0x2000, CRC(75070978) SHA1(7f59460c094e596a521014f956d76e5c714022a2) )
	ROM_LOAD( "8a.bin", 0x4000, 0x2000, CRC(0e803777) SHA1(bccc182ccbd7312fc6545ffcef4d54637416dae7) )
	ROM_LOAD( "c4.a8",  0x6000, 0x2000, CRC(c7f5a247) SHA1(de79341f9c6c7032f76cead46d614e13d4af50f9) )

	ROM_REGION( 0x2000, "gfx", 0 )
	ROM_LOAD( "4h.bin", 0x0000, 0x1000, CRC(67dd6c19) SHA1(d3dc0cb9b108c2584c4844fc0eb4c9ee170986fe) )
	ROM_LOAD( "5h.bin", 0x1000, 0x1000, CRC(50fba1d4) SHA1(40ba480713284ae484c6687490f91bf62a7167e1) )

	ROM_REGION( 0x0120, "proms", 0 )
	ROM_LOAD( "clr.j7", 0x0000, 0x0020, CRC(ee1cf1d5) SHA1(4f4cfde1a896da92d8265889584dd0c5678de033) )
	ROM_LOAD( "clr.g5", 0x0020, 0x0100, CRC(bcb1e2e3) SHA1(c09731836a9d4e50316a84b86f61b599a1ef944d) )
ROM_END

} // anonymous namespace


/*************************************
 *
 *  Game driver(s)
 *
 *************************************/

GAME( 1983, mrjong,   0,      mrjong, mrjong, mrjong_state, empty_init, ROT90, "Kiwako",               "Mr. Jong (Japan)", MACHINE_SUPPORTS_SAVE )
GAME( 1983, crazyblk, mrjong, mrjong, mrjong, mrjong_state, empty_init, ROT90, "Kiwako (ECI license)", "Crazy Blocks",     MACHINE_SUPPORTS_SAVE )
GAME( 1983, blkbustr, mrjong, mrjong, mrjong, mrjong_state, empty_init, ROT90, "Kiwako (ECI license)", "BlockBuster",      MACHINE_SUPPORTS_SAVE )
