#ifndef SRC_MVLC_MVLC_MVP_FLASH_H
#define SRC_MVLC_MVLC_MVP_FLASH_H

#include <mesytec-mvlc/mesytec-mvlc.h>
#include <flash.h>
#include <firmware.h>
#include <firmware_ops.h>

namespace mesytec::mvme_mvlc::mvp
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

        void setMvlc(mvlc::MVLC &mvlc);
        mvlc::MVLC getMvlc() const;
        void setVmeAddress(mvlc::u32 vmeAddress);
        mvlc::u32 getVmeAddress() const;



    private:
        mvlc::MVLC mvlc_;
        mvlc::u32 vmeAddress_ = 0;
        bool isFlashEnabled_ = false;
};

};

#endif // SRC_MVLC_MVLC_MVP_FLASH_H
