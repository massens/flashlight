/*
 * Copyright (c) Facebook, Inc. and its affiliates.
 *
 * This source code is licensed under the BSD-style license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include "flashlight/app/asr/augmentation/AdditiveNoise.h"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <sstream>

#include "flashlight/app/asr/augmentation/SoundEffectUtil.h"
#include "flashlight/app/asr/data/Sound.h"
#include "flashlight/fl/common/Logging.h"

namespace fl {
namespace app {
namespace asr {
namespace sfx {

std::string AdditiveNoise::Config::prettyString() const {
  std::stringstream ss;
  ss << "AdditiveNoise::Config{ratio_=" << ratio_ << " minSnr_=" << minSnr_
     << " maxSnr_=" << maxSnr_ << " nClipsMin_=" << nClipsMin_ << " nClipsMax_"
     << nClipsMax_ << " listFilePath_=" << listFilePath_
     << " dsetRndPolicy_=" << randomPolicyToString(dsetRndPolicy_)
     << " randomSeed_=" << randomSeed_ << '}';
  return ss.str();
}

std::string AdditiveNoise::prettyString() const {
  std::stringstream ss;
  ss << "AdditiveNoise{config={" << conf_.prettyString() << '}'
     << " ListRandomizer_={" << ListRandomizer_->prettyString() << "}";
  return ss.str();
};

AdditiveNoise::AdditiveNoise(const AdditiveNoise::Config& config)
    : conf_(config),
      rng_(config.randomSeed_) {
  std::ifstream listFile(conf_.listFilePath_);
  if (!listFile) {
    throw std::runtime_error(
        "AdditiveNoise failed to open listFilePath_=" + conf_.listFilePath_);
  }
  std::vector<std::string> noiseFiles;
  while (!listFile.eof()) {
    try {
      std::string filename;
      std::getline(listFile, filename);
      if (!filename.empty()) {
        noiseFiles.push_back(filename);
      }
    } catch (std::exception& ex) {
      throw std::runtime_error(
          "AdditiveNoise failed to read listFilePath_=" + conf_.listFilePath_ +
          " with error=" + ex.what());
    }
  }

  ListRandomizer<std::string>::Config dsConf;
  dsConf.policy_ = conf_.dsetRndPolicy_;
  dsConf.randomSeed_ = conf_.randomSeed_;
  ListRandomizer_ = std::make_unique<ListRandomizer<std::string>>(
      dsConf, std::move(noiseFiles));
}

void AdditiveNoise::apply(std::vector<float>& signal) {
  if (rng_.random() >= conf_.proba_) {
    return;
  }
  const float signalRms = rootMeanSquare(signal);
  const float snr = rng_.uniform(conf_.minSnr_, conf_.maxSnr_);
  const int nClips = rng_.randInt(conf_.nClipsMin_, conf_.nClipsMax_);
  int augStart = rng_.randInt(0, signal.size() - 1);
  // overflow implies we start at the beginning again.
  int augEnd = augStart + conf_.ratio_ * signal.size();

  std::vector<float> mixedNoise(signal.size(), 0.0f);
  for (int i = 0; i < nClips; ++i) {
    auto curNoise = loadSound<float>(ListRandomizer_->getRandom());
    int shift = rng_.randInt(0, curNoise.size() - 1);
    for (int j = augStart; j < augEnd; ++j) {
      mixedNoise[j % mixedNoise.size()] +=
          curNoise[(shift + j) % curNoise.size()];
    }
  }

  const float noiseRms = rootMeanSquare(mixedNoise);
  if (noiseRms > 0) {
    // https://en.wikipedia.org/wiki/Signal-to-noise_ratio
    const float noiseMult =
      (signalRms / (noiseRms * std::pow(10, snr / 20.0)));
    for (int i = 0; i < signal.size(); ++i) {
      signal[i] += mixedNoise[i] * noiseMult;
    }
  } else {
    FL_LOG(fl::WARNING) << "AdditiveNoise::apply() invalid noiseRms="
                        << noiseRms;
  }
}

} // namespace sfx
} // namespace asr
} // namespace app
} // namespace fl
