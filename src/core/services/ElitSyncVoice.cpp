#include "core/services/ElitSyncVoice.h"
#include "core/services/ElitInvoiceReader.h"
#include "core/util/SoundSupport.h"
#include "infrastructure/db/sqlite/Db.h"
#include <QCoreApplication>
#include <QThread>
#include <QUrl>
#include <QVariant>
#include <QTimer>
#include <QtEndian>
#if CS_HAS_MULTIMEDIA
#include <QSoundEffect>
#elif defined(Q_OS_WIN)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <mmsystem.h>
#endif

namespace ElitSyncVoice {
Outcome classify(bool committed,bool cancelled,ElitInvoiceReader::Failure failure) {
    if(committed) return Outcome::Success;
    if(cancelled || failure==ElitInvoiceReader::Failure::Cancelled) return Outcome::Cancelled;
    if(failure==ElitInvoiceReader::Failure::Incomplete) return Outcome::Incomplete;
    return Outcome::Error;
}
QString assetName(Outcome outcome) {
    switch(outcome) {
    case Outcome::Success: return "elit_sync_success.wav";
    case Outcome::Cancelled: return "elit_sync_cancelled.wav";
    case Outcome::Incomplete: return "elit_sync_incomplete.wav";
    default: return "elit_sync_error.wav";
    }
}
bool enabled() { return Database::getSetting("settings.sounds.elit_voice",true).toBool(); }
#if CS_HAS_MULTIMEDIA
namespace {
QSoundEffect* effect() {
    static auto* value=new QSoundEffect(QCoreApplication::instance());
    return value;
}
}
#endif
#if !CS_HAS_MULTIMEDIA && defined(Q_OS_WIN)
namespace {
// A dedicated waveOut stream prevents ordinary button/error sounds from cutting
// off speech. Scale PCM samples instead of changing the Windows device volume.
class NativeVoice final : public QObject {
public:
    NativeVoice():QObject(QCoreApplication::instance()) {
        timer.setInterval(50);
        connect(&timer,&QTimer::timeout,this,[this]{if(header.dwFlags&WHDR_DONE) stop();});
        connect(QCoreApplication::instance(),&QCoreApplication::aboutToQuit,this,[this]{stop();});
    }
    ~NativeVoice() override { stop(); }
    void stop() {
        timer.stop();
        if(device) {
            waveOutReset(device);
            if(header.dwFlags&WHDR_PREPARED) waveOutUnprepareHeader(device,&header,sizeof(header));
            waveOutClose(device);device=nullptr;
        }
        header={};pcm.clear();
    }
    bool play(const QByteArray& wav,int volume) {
        stop();
        if(wav.size()<12 || wav.first(4)!="RIFF" || wav.mid(8,4)!="WAVE") return false;
        WAVEFORMATEX format{};
        for(qsizetype offset=12;offset+8<=wav.size();) {
            const auto size=qFromLittleEndian<quint32>(wav.constData()+offset+4);
            const auto start=offset+8;
            if(size>quint64(wav.size()-start)) return false;
            const auto tag=wav.mid(offset,4);
            if(tag=="fmt " && size>=16) {
                format.wFormatTag=qFromLittleEndian<quint16>(wav.constData()+start);
                format.nChannels=qFromLittleEndian<quint16>(wav.constData()+start+2);
                format.nSamplesPerSec=qFromLittleEndian<quint32>(wav.constData()+start+4);
                format.nAvgBytesPerSec=qFromLittleEndian<quint32>(wav.constData()+start+8);
                format.nBlockAlign=qFromLittleEndian<quint16>(wav.constData()+start+12);
                format.wBitsPerSample=qFromLittleEndian<quint16>(wav.constData()+start+14);
            } else if(tag=="data") pcm=wav.mid(start,size);
            offset=start+size+(size&1);
        }
        if(format.wFormatTag!=WAVE_FORMAT_PCM || format.wBitsPerSample!=16 || format.nChannels!=1
            || format.nSamplesPerSec!=24000 || format.nBlockAlign!=2 || format.nAvgBytesPerSec!=48000
            || pcm.isEmpty() || pcm.size()%2) return false;
        for(qsizetype offset=0;offset<pcm.size();offset+=2) {
            const auto sample=qFromLittleEndian<qint16>(pcm.constData()+offset);
            qToLittleEndian<qint16>(qint16(int(sample)*qBound(0,volume,100)/100),pcm.data()+offset);
        }
        if(waveOutOpen(&device,WAVE_MAPPER,&format,0,0,CALLBACK_NULL)!=MMSYSERR_NOERROR) {device=nullptr;return false;}
        header.lpData=pcm.data();header.dwBufferLength=DWORD(pcm.size());
        if(waveOutPrepareHeader(device,&header,sizeof(header))!=MMSYSERR_NOERROR
            || waveOutWrite(device,&header,sizeof(header))!=MMSYSERR_NOERROR) {stop();return false;}
        timer.start();return true;
    }
private:
    HWAVEOUT device=nullptr;
    WAVEHDR header{};
    QByteArray pcm;
    QTimer timer;
};
NativeVoice* nativeVoice() { static auto* voice=new NativeVoice;return voice; }
}
#endif
bool play(Outcome outcome) {
    if(!QCoreApplication::instance() || QThread::currentThread()!=QCoreApplication::instance()->thread()) return false;
    if(!enabled() || SoundSupport::soundsVolumePct(85)==0) return false;
#if CS_HAS_MULTIMEDIA
    auto* sound=effect();sound->stop();
    sound->setSource(QUrl("qrc:/assets/"+assetName(outcome)));
    sound->setLoopCount(1);
    sound->setVolume(SoundSupport::soundsVolumePct(85)/100.0f);
    sound->play();
    return true;
#elif defined(Q_OS_WIN)
    const auto bytes=SoundSupport::loadAssetBytes(assetName(outcome));
    return nativeVoice()->play(bytes,SoundSupport::soundsVolumePct(85));
#else
    return false;
#endif
}
void stop() {
#if CS_HAS_MULTIMEDIA
    effect()->stop();
#elif defined(Q_OS_WIN)
    nativeVoice()->stop();
#endif
}
}
