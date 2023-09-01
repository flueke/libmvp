#include "mvlc_mvp_flash.h"

using namespace mesytec::mvlc;

namespace mesytec::mvme_mvlc::mvp
{

void MvlcMvpFlash::setMvlc(MVLC &mvlc)
{
    mvlc_ = mvlc;
    isFlashEnabled_ = false;
    m_write_enabled = false;
}

MVLC MvlcMvpFlash::getMvlc() const
{
    return MVLC();
}

void MvlcMvpFlash::setVmeAddress(u32 vmeAddress)
{
    vmeAddress_ = vmeAddress;
    isFlashEnabled_ = false;
    m_write_enabled = false;
}

u32 MvlcMvpFlash::getVmeAddress() const
{
    return vmeAddress_;
}

void MvlcMvpFlash::write_instruction(const gsl::span<uchar> data, int timeout_ms)
{
    if (auto ec = write_instruction(mvlc_, vmeAddress_,
}

void MvlcMvpFlash::read_response(gsl::span<uchar> dest, int timeout_ms)
{
}

void MvlcMvpFlash::write_page(const Address &address, uchar section, const gsl::span<uchar> data, int timeout_ms)
{
}

void MvlcMvpFlash::read_page(const Address &address, uchar section, gsl::span<uchar> dest, int timeout_ms)
{
}

void MvlcMvpFlash::recover(size_t tries)
{
}

}
