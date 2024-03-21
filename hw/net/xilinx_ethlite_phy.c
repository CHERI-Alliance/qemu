#define DPHY(x)

/* Advertisement control register. */
#define ADVERTISE_10FULL        0x0040  /* Try for 10mbps full-duplex  */
#define ADVERTISE_100HALF       0x0080  /* Try for 100mbps half-duplex */
#define ADVERTISE_100FULL       0x0100  /* Try for 100mbps full-duplex */

#define CONTROL_PAYLOAD_WORDS 5
#define CONTROL_PAYLOAD_SIZE (CONTROL_PAYLOAD_WORDS * (sizeof(uint32_t)))

struct PHY {
    uint32_t regs[32];

    int link;

    unsigned int (*read)(struct PHY *phy, unsigned int req);
    void (*write)(struct PHY *phy, unsigned int req,
                  unsigned int data);
};

static unsigned int tdk_read(struct PHY *phy, unsigned int req)
{
    int regnum;
    unsigned r = 0;

    regnum = req & 0x1f;

    switch (regnum) {
        case 1:
            if (!phy->link) {
                break;
            }
            /* MR1.  */
            /* Speeds and modes.  */
            r |= (1 << 13) | (1 << 14);
            r |= (1 << 11) | (1 << 12);
            r |= (1 << 5); /* Autoneg complete.  */
            r |= (1 << 3); /* Autoneg able.  */
            r |= (1 << 2); /* link.  */
            r |= (1 << 1); /* link.  */
            break;
        case 5:
            /* Link partner ability.
               We are kind; always agree with whatever best mode
               the guest advertises.  */
            r = 1 << 14; /* Success.  */
            /* Copy advertised modes.  */
            r |= phy->regs[4] & (15 << 5);
            /* Autoneg support.  */
            r |= 1;
            break;
        case 17:
            /* Marvell PHY on many xilinx boards.  */
            r = 0x8000; /* 1000Mb  */
            break;
        case 18:
            {
                /* Diagnostics reg.  */
                int duplex = 0;
                int speed_100 = 0;

                if (!phy->link) {
                    break;
                }

                /* Are we advertising 100 half or 100 duplex ? */
                speed_100 = !!(phy->regs[4] & ADVERTISE_100HALF);
                speed_100 |= !!(phy->regs[4] & ADVERTISE_100FULL);

                /* Are we advertising 10 duplex or 100 duplex ? */
                duplex = !!(phy->regs[4] & ADVERTISE_100FULL);
                duplex |= !!(phy->regs[4] & ADVERTISE_10FULL);
                r = (speed_100 << 10) | (duplex << 11);
            }
            break;

        default:
            r = phy->regs[regnum];
            break;
    }
    DPHY(qemu_log("\n%s %x = reg[%d]\n", __func__, r, regnum));
    return r;
}

static void
tdk_write(struct PHY *phy, unsigned int req, unsigned int data)
{
    int regnum;

    regnum = req & 0x1f;
    DPHY(qemu_log("%s reg[%d] = %x\n", __func__, regnum, data));
    switch (regnum) {
        default:
            phy->regs[regnum] = data;
            break;
    }

    /* Unconditionally clear regs[BMCR][BMCR_RESET] and auto-neg */
    phy->regs[0] &= ~0x8200;
}

static void
tdk_init(struct PHY *phy)
{
    phy->regs[0] = 0x3100;
    /* PHY Id.  */
    phy->regs[2] = 0x0300;
    phy->regs[3] = 0xe400;
    /* Autonegotiation advertisement reg.  */
    phy->regs[4] = 0x01E1;
    phy->link = 1;

    phy->read = tdk_read;
    phy->write = tdk_write;
}

struct MDIOBus {
    struct PHY *devs[32];
};

static void
mdio_attach(struct MDIOBus *bus, struct PHY *phy, unsigned int addr)
{
    bus->devs[addr & 0x1f] = phy;
}

#ifdef USE_THIS_DEAD_CODE
static void
mdio_detach(struct MDIOBus *bus, struct PHY *phy, unsigned int addr)
{
    bus->devs[addr & 0x1f] = NULL;
}
#endif

static uint16_t mdio_read_req(struct MDIOBus *bus, unsigned int addr,
                  unsigned int reg)
{
    struct PHY *phy;
    uint16_t data;

    phy = bus->devs[addr];
    if (phy && phy->read) {
        data = phy->read(phy, reg);
    } else {
        data = 0xffff;
    }
    DPHY(qemu_log("%s addr=%d reg=%d data=%x\n", __func__, addr, reg, data));
    return data;
}

static void mdio_write_req(struct MDIOBus *bus, unsigned int addr,
               unsigned int reg, uint16_t data)
{
    struct PHY *phy;

    DPHY(qemu_log("%s addr=%d reg=%d data=%x\n", __func__, addr, reg, data));
    phy = bus->devs[addr];
    if (phy && phy->write) {
        phy->write(phy, reg, data);
    }
}

