#include "mvlc_mvp_flash.h"
#include "mvlc_mvp_lib.h"

using namespace mesytec::mvlc;
using namespace mesytec::mvp;

namespace mesytec::mvp
{

MvlcMvpFlash::~MvlcMvpFlash()
{
    maybe_disable_flash_interface();
}

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
    maybe_disable_flash_interface();
    vmeAddress_ = vmeAddress;
    m_write_enabled = false;
}

u32 MvlcMvpFlash::getVmeAddress() const
{
    return vmeAddress_;
}

void MvlcMvpFlash::maybe_enable_flash_interface()
{
    if (!isFlashEnabled_)
    {
        if (auto ec = enable_flash_interface(mvlc_, vmeAddress_))
            throw std::system_error(ec);
        isFlashEnabled_ = true;
    }
}

void MvlcMvpFlash::maybe_disable_flash_interface()
{
    if (isFlashEnabled_)
    {
        if (auto ec = disable_flash_interface(mvlc_, vmeAddress_))
        {
            auto msg = QSL("Warning: could not disable flash interface on 0x%1.")
                .arg(vmeAddress_, 8, 16, QLatin1Char('0'));
            emit progress_text_changed(msg);
        }
        isFlashEnabled_ = false;
    }
}

void MvlcMvpFlash::write_instruction(const gsl::span<uchar> data, int timeout_ms)
{
    (void) timeout_ms;
    maybe_enable_flash_interface();

    if (auto ec = mesytec::mvp::write_instruction(mvlc_, vmeAddress_, data))
        throw std::system_error(ec);

    emit instruction_written(span_to_qvector(data));
}

void MvlcMvpFlash::read_response(gsl::span<uchar> dest, int timeout_ms)
{
    (void) timeout_ms;
    maybe_enable_flash_interface();

    std::vector<u8> tmpDest;
    if (auto ec = mesytec::mvp::read_response(mvlc_, vmeAddress_, tmpDest))
        throw std::system_error(ec);

    std::copy(std::begin(tmpDest), std::begin(tmpDest) + std::min(tmpDest.size(), dest.size()), std::begin(dest));

    emit response_read(span_to_qvector(dest));
}

void MvlcMvpFlash::write_page(const Address &address, uchar section, const gsl::span<uchar> data, int timeout_ms)
{
    (void) timeout_ms;
    maybe_enable_flash_interface();
    maybe_set_verbose(false);
    maybe_enable_write();

    std::vector<u8> pageBuffer;
    pageBuffer.reserve(data.size());
    std::copy(std::begin(data), std::end(data), std::back_inserter(pageBuffer));
    if (auto ec = mesytec::mvp::write_page4(mvlc_, vmeAddress_, address.data() , section, pageBuffer))
        throw std::system_error(ec);

    emit data_written(span_to_qvector(data));
}

void MvlcMvpFlash::read_page(const Address &address, uchar section, gsl::span<uchar> dest, int timeout_ms)
{
    (void) timeout_ms;
    maybe_enable_flash_interface();
    maybe_set_verbose(false);

    std::vector<u8> pageBuffer;
    pageBuffer.reserve(dest.size());
    if (auto ec = mesytec::mvp::read_page(mvlc_, vmeAddress_, address.data(), section,
        dest.size(), pageBuffer))
        throw std::system_error(ec);

    std::copy(std::begin(pageBuffer), std::begin(pageBuffer) + std::min(pageBuffer.size(), dest.size()), std::begin(dest));
}

void MvlcMvpFlash::recover(size_t tries)
{
    // Attempt this only once, letting any exception terminate this method.
    maybe_enable_flash_interface();

    std::exception_ptr last_nop_exception;

    for (auto n=0u; n<tries; ++n)
    {
        try
        {
            nop();
            return;
        }
        catch(const std::system_error &e)
        {
            if (e.code() == std::errc::timed_out)
                throw;
            last_nop_exception = std::current_exception();
            clear_output_fifo(mvlc_, vmeAddress_);
        }
        catch(const std::exception& e)
        {
            last_nop_exception = std::current_exception();
            clear_output_fifo(mvlc_, vmeAddress_);
        }
    }

    if (last_nop_exception)
        std::rethrow_exception(last_nop_exception);
    else
        throw std::runtime_error("NOP recovery failed for an unknown reason");
}

void MvlcMvpFlash::erase_section(uchar section)
{
    maybe_enable_flash_interface();
    maybe_enable_write();
    if (auto ec = mesytec::mvp::erase_section(mvlc_, vmeAddress_, section))
        throw std::system_error(ec);
}

void MvlcMvpFlash::write_memory(const Address &start, uchar section, const gsl::span<uchar> mem)
{
    // Note (230919): write_pages() does not lead to a noticeable speedup when
    // flashing firmware packages. The reason is that the stack uploads
    // (consisting purely of "super" commands) are still transaction based,
    // meaning the PC has to wait for a response from the MVLC for each part of
    // the stack that's been written. The core implementation will have to
    // change to allow "blindly" sending out all the partial stack uploads to
    // the MVLC before waiting for a response. This could lead to a massive
    // speedup due to less command ping-pong happening.
    #if 0
    // Split mem into page sized parts and pass up to two parts to
    // write_pages()
    Address addr(start);
    size_t remaining = mem.size();
    size_t offset    = 0;
    size_t callsToWritePages = 0;

    emit progress_range_changed(0, mem.size());

    while (remaining) {
        auto len = std::min(constants::page_size * 2, remaining);
        gsl::span page1(mem.data() + offset, std::min(constants::page_size, len));
        gsl::span page2(mem.data() + offset + page1.size(), std::min(constants::page_size, len - page1.size()));

        assert(page1.size() + page2.size() == len);

        if (auto ec = write_pages(mvlc_, vmeAddress_, addr.to_int(), section, page1, page2))
            throw std::system_error(ec);

        ++callsToWritePages;
        remaining -= len;
        addr      += len;
        offset    += len;

        emit progress_changed(offset);
    }

    qDebug() << __PRETTY_FUNCTION__ << "write_memory(): needed" << callsToWritePages << "calls to write_pages()";

    #else
    FlashInterface::write_memory(start, section, mem);
    #endif
}

void MvlcMvpFlash::boot(uchar area_index)
{
    std::array<uchar, 4> data = { opcodes::BFP, constants::access_code[0], constants::access_code[1], area_index };
    write_instruction(gsl::span<uchar>(data.data(), data.size()));
    // Deliberately not attempting to read a response: it will only result in a
    // "no VME response" error as the module is rebooting.
}

void MvlcMvpFlash::ensure_response_ok(
    const gsl::span<uchar> &instruction,
    const gsl::span<uchar> &response)
{
    std::vector<u8> v_instruction(std::begin(instruction), std::end(instruction));
    std::vector<u8> v_response(std::begin(response), std::end(response));

    if (!check_response(v_instruction, v_response))
        throw FlashInstructionError(instruction, response, "check_response() not ok");
}

}
