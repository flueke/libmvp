#ifndef SRC_MVLC_MVLC_MVP_FLASH_H
#define SRC_MVLC_MVLC_MVP_FLASH_H

#include <mesytec-mvlc/mesytec-mvlc.h>
#include <firmware.h>
#include <firmware_ops.h>
#include <flash_constants.h>
#include <flash.h>

namespace mesytec::mvp
{

class MvlcMvpFlash: public mesytec::mvp::FlashInterface
{
    Q_OBJECT
    public:
        MvlcMvpFlash(QObject *parent = nullptr)
            : FlashInterface(parent)
        {}

        MvlcMvpFlash(mvlc::MVLC &mvlc, mvlc::u32 vmeAddress, QObject *parent = nullptr)
            : FlashInterface(parent)
            , mvlc_(mvlc)
            , vmeAddress_(vmeAddress)
        {}

        ~MvlcMvpFlash() override;

        void setMvlc(mvlc::MVLC &mvlc);
        mvlc::MVLC getMvlc() const;
        void setVmeAddress(mvlc::u32 vmeAddress);
        mvlc::u32 getVmeAddress() const;

        void maybe_enable_flash_interface();
        void maybe_disable_flash_interface();

        void write_instruction(const gsl::span<uchar> data,
          int timeout_ms = constants::default_timeout_ms) override;

        void read_response(gsl::span<uchar> dest,
          int timeout_ms = constants::default_timeout_ms) override;

        void write_page(const Address &address, uchar section,
          const gsl::span<uchar> data, int timeout_ms = constants::data_timeout_ms) override;

        void read_page(const Address &address, uchar section, gsl::span<uchar> dest,
          int timeout_ms = constants::data_timeout_ms) override;

        void recover(size_t tries=default_recover_tries) override;

        void erase_section(uchar section) override;

        void write_memory(const Address &start, uchar section, const gsl::span<uchar> data) override;

        // Custom boot() ignoring the missing VME response.
        void boot(uchar area_index) override;

        // Custom version of ensure_response_ok() with workarounds for some VME
        // MVP interface issues.
        void ensure_response_ok(
          const gsl::span<uchar> &instruction,
          const gsl::span<uchar> &response) override;

    private:
        mvlc::MVLC mvlc_;
        mvlc::u32 vmeAddress_ = 0;
        bool isFlashEnabled_ = false;
};

};

#endif // SRC_MVLC_MVLC_MVP_FLASH_H
