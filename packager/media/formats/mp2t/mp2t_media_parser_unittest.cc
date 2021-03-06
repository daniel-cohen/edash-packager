// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <gtest/gtest.h>

#include <algorithm>
#include <string>

#include "packager/base/bind.h"
#include "packager/base/bind_helpers.h"
#include "packager/base/logging.h"
#include "packager/base/memory/ref_counted.h"
#include "packager/media/base/media_sample.h"
#include "packager/media/base/stream_info.h"
#include "packager/media/base/timestamp.h"
#include "packager/media/base/video_stream_info.h"
#include "packager/media/formats/mp2t/mp2t_common.h"
#include "packager/media/formats/mp2t/mp2t_media_parser.h"
#include "packager/media/test/test_data_util.h"

namespace edash_packager {
namespace media {
namespace mp2t {

class Mp2tMediaParserTest : public testing::Test {
 public:
  Mp2tMediaParserTest()
      : audio_frame_count_(0),
        video_frame_count_(0),
        video_min_dts_(kNoTimestamp),
        video_max_dts_(kNoTimestamp) {
    parser_.reset(new Mp2tMediaParser());
  }

 protected:
  typedef std::map<int, scoped_refptr<StreamInfo> > StreamMap;

  scoped_ptr<Mp2tMediaParser> parser_;
  StreamMap stream_map_;
  int audio_frame_count_;
  int video_frame_count_;
  int64_t video_min_dts_;
  int64_t video_max_dts_;

  bool AppendData(const uint8_t* data, size_t length) {
    return parser_->Parse(data, length);
  }

  bool AppendDataInPieces(const uint8_t* data,
                          size_t length,
                          size_t piece_size) {
    const uint8_t* start = data;
    const uint8_t* end = data + length;
    while (start < end) {
      size_t append_size = std::min(piece_size,
                                    static_cast<size_t>(end - start));
      if (!AppendData(start, append_size))
        return false;
      start += append_size;
    }
    return true;
  }

  void OnInit(const std::vector<scoped_refptr<StreamInfo> >& stream_infos) {
    DVLOG(1) << "OnInit: " << stream_infos.size() << " streams.";
    for (std::vector<scoped_refptr<StreamInfo> >::const_iterator iter =
             stream_infos.begin(); iter != stream_infos.end(); ++iter) {
      DVLOG(1) << (*iter)->ToString();
      stream_map_[(*iter)->track_id()] = *iter;
    }
  }

  bool OnNewSample(uint32_t track_id,
                   const scoped_refptr<MediaSample>& sample) {
    std::string stream_type;
    StreamMap::const_iterator stream = stream_map_.find(track_id);
    if (stream != stream_map_.end()) {
      if (stream->second->stream_type() == kStreamAudio) {
        ++audio_frame_count_;
        stream_type = "audio";
      } else if (stream->second->stream_type() == kStreamVideo) {
        ++video_frame_count_;
        stream_type = "video";
        if (video_min_dts_ == kNoTimestamp)
          video_min_dts_ = sample->dts();
        // Verify timestamps are increasing.
        if (video_max_dts_ == kNoTimestamp)
          video_max_dts_ = sample->dts();
        else if (video_max_dts_ >= sample->dts()) {
          LOG(ERROR) << "Video DTS not strictly increasing.";
          return false;
        }
        video_max_dts_ = sample->dts();
      } else {
        LOG(ERROR) << "Missing StreamInfo for track ID " << track_id;
        return false;
      }
    }

    return true;
  }

  void InitializeParser() {
    parser_->Init(
        base::Bind(&Mp2tMediaParserTest::OnInit,
                   base::Unretained(this)),
        base::Bind(&Mp2tMediaParserTest::OnNewSample,
                   base::Unretained(this)),
        NULL);
  }

  bool ParseMpeg2TsFile(const std::string& filename, int append_bytes) {
    InitializeParser();

    std::vector<uint8_t> buffer = ReadTestDataFile(filename);
    EXPECT_TRUE(AppendDataInPieces(buffer.data(),
                                   buffer.size(),
                                   append_bytes));
    return true;
  }
};

TEST_F(Mp2tMediaParserTest, UnalignedAppend17) {
  // Test small, non-segment-aligned appends.
  ParseMpeg2TsFile("bear-1280x720.ts", 17);
  EXPECT_EQ(video_frame_count_, 80);
  parser_->Flush();
  EXPECT_EQ(video_frame_count_, 82);
}

TEST_F(Mp2tMediaParserTest, UnalignedAppend512) {
  // Test small, non-segment-aligned appends.
  ParseMpeg2TsFile("bear-1280x720.ts", 512);
  EXPECT_EQ(video_frame_count_, 80);
  parser_->Flush();
  EXPECT_EQ(video_frame_count_, 82);
}

TEST_F(Mp2tMediaParserTest, TimestampWrapAround) {
  // "bear-1280x720_ptswraparound.ts" has been transcoded
  // from bear-1280x720.mp4 by applying a time offset of 95442s
  // (close to 2^33 / 90000) which results in timestamps wrap around
  // in the Mpeg2 TS stream.
  ParseMpeg2TsFile("bear-1280x720_ptswraparound.ts", 512);
  parser_->Flush();
  EXPECT_EQ(video_frame_count_, 82);
  EXPECT_GE(video_min_dts_, (95443 - 1) * kMpeg2Timescale);
  EXPECT_LE(video_max_dts_,
            static_cast<int64_t>((95443 + 4)) * kMpeg2Timescale);
}

}  // namespace mp2t
}  // namespace media
}  // namespace edash_packager
