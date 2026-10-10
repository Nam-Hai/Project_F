#pragma once

#include <string>
#include <vector>

#include "../../core/ui/sample_library.h"
#include "sample_bank.h"

// Les samples d'un dossier du disque (PC). Ne liste que les WAV au bon format (mono, 16 bits,
// 44,1 kHz) ; les autres sont signalés sur stderr au scan. Les buffers sont chargés dans la
// SampleBank partagée.
class DesktopSampleLibrary : public SampleLibrary {
public:
    DesktopSampleLibrary(const std::string& directory, SampleBank& bank);

    // Relit le dossier (nouveaux fichiers déposés, par exemple).
    void refresh();

    int         count() const override { return static_cast<int>(names_.size()); }
    const char* name(int index) const override { return names_[static_cast<std::size_t>(index)].c_str(); }
    int         find(const char* file) const override;
    bool        load(int index, const int16_t*& data, uint32_t& length) override;
    int         channels(const int16_t* data) const override { return bank_.channels(data); }
    void        discard(const int16_t* data) override { bank_.release(data); }

private:
    std::string              directory_;
    SampleBank&              bank_;
    std::vector<std::string> names_;
};
