#pragma once
#include <QString>
namespace ElitInvoiceReader { enum class Failure; }

namespace ElitSyncVoice {
enum class Outcome { Success, Error, Cancelled, Incomplete };
Outcome classify(bool committed, bool cancelled, ElitInvoiceReader::Failure failure);
QString assetName(Outcome outcome);
bool enabled();
bool play(Outcome outcome);
void stop();
}
