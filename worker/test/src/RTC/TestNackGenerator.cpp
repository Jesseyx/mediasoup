#include "common.hpp"
#include "catch.hpp"
#include "RTC/Codecs/PayloadDescriptorHandler.hpp"
#include "RTC/NackGenerator.hpp"
#include "RTC/RtpPacket.hpp"
#include <vector>

using namespace RTC;

struct TestNackGeneratorInput
{
	TestNackGeneratorInput() = default;
	TestNackGeneratorInput(
	  uint16_t seq,
	  bool isKeyFrame,
	  uint16_t firstNacked,
	  size_t numNacked,
	  bool keyFrameRequired = false,
	  size_t nackListSize   = 0)
	  : seq(seq), isKeyFrame(isKeyFrame), firstNacked(firstNacked), numNacked(numNacked),
	    keyFrameRequired(keyFrameRequired), nackListSize(nackListSize)
	{
	}

	uint16_t seq{ 0 };
	bool isKeyFrame{ false };
	uint16_t firstNacked{ 0 };
	size_t numNacked{ 0 };
	bool keyFrameRequired{ false };
	size_t nackListSize{ 0 };
};

class TestPayloadDescriptorHandler : public Codecs::PayloadDescriptorHandler
{
public:
	explicit TestPayloadDescriptorHandler(bool isKeyFrame) : isKeyFrame(isKeyFrame){};
	~TestPayloadDescriptorHandler() = default;
	void Dump() const
	{
		return;
	};
	bool Process(Codecs::EncodingContext* /*context*/, uint8_t* /*data*/, bool& /*marker*/)
	{
		return true;
	};
	void Restore(uint8_t* /*data*/)
	{
		return;
	};
	uint8_t GetSpatialLayer() const
	{
		return 0;
	};
	uint8_t GetTemporalLayer() const
	{
		return 0;
	};
	bool IsKeyFrame() const
	{
		return this->isKeyFrame;
	};

private:
	bool isKeyFrame{ false };
};

class TestNackGeneratorListener : public NackGenerator::Listener
{
	void OnNackGeneratorNackRequired(const std::vector<uint16_t>& seqNumbers) override
	{
		this->nackRequiredTriggered = true;

		auto it          = seqNumbers.begin();
		auto firstNacked = *it;

		auto numNacked = seqNumbers.size();

		REQUIRE(this->currentInput.firstNacked == firstNacked);
		REQUIRE(this->currentInput.numNacked == numNacked);
	};

	void OnNackGeneratorKeyFrameRequired() override
	{
		this->keyFrameRequiredTriggered = true;

		REQUIRE(this->currentInput.keyFrameRequired);
	}

public:
	void Reset(TestNackGeneratorInput& input)
	{
		this->currentInput              = input;
		this->nackRequiredTriggered     = false;
		this->keyFrameRequiredTriggered = false;
	}

	void Check(NackGenerator& nackGenerator)
	{
		REQUIRE(this->nackRequiredTriggered == static_cast<bool>(this->currentInput.numNacked));
		REQUIRE(this->keyFrameRequiredTriggered == this->currentInput.keyFrameRequired);

		REQUIRE(nackGenerator.GetNackListLength() == this->currentInput.nackListSize);
	}

private:
	TestNackGeneratorInput currentInput{};
	bool nackRequiredTriggered     = false;
	bool keyFrameRequiredTriggered = false;
};

// clang-format off
uint8_t rtpBuffer[] =
{
	0b10000000, 0b01111011, 0b01010010, 0b00001110,
	0b01011011, 0b01101011, 0b11001010, 0b10110101,
	0, 0, 0, 2
};
// clang-format on

// [pt:123, seq:21006, timestamp:1533790901]
RtpPacket* packet = RtpPacket::Parse(rtpBuffer, sizeof(rtpBuffer));

void validate(std::vector<TestNackGeneratorInput>& inputs)
{
	TestNackGeneratorListener listener;
	NackGenerator nackGenerator = NackGenerator(&listener);

	for (auto input : inputs)
	{
		listener.Reset(input);

		TestPayloadDescriptorHandler* tpdh = new TestPayloadDescriptorHandler(input.isKeyFrame);

		packet->SetPayloadDescriptorHandler(tpdh);
		packet->SetSequenceNumber(input.seq);
		nackGenerator.ReceivePacket(packet);

		listener.Check(nackGenerator);
	}
};

SCENARIO("NACK generator", "[rtp][rtcp]")
{
	SECTION("no NACKs required")
	{
		// clang-format off
		std::vector<TestNackGeneratorInput> inputs =
		{
			{ 2371, false, 0, 0, false, 0 },
			{ 2372, false, 0, 0, false, 0 },
			{ 2373, false, 0, 0, false, 0 },
			{ 2374, false, 0, 0, false, 0 },
			{ 2375, false, 0, 0, false, 0 },
			{ 2376, false, 0, 0, false, 0 },
			{ 2377, false, 0, 0, false, 0 },
			{ 2378, false, 0, 0, false, 0 },
			{ 2379, false, 0, 0, false, 0 },
			{ 2380, false, 0, 0, false, 0 },
			{ 2254, false, 0, 0, false, 0 },
			{ 2250, false, 0, 0, false, 0 },
		};
		// clang-format on

		validate(inputs);
	}

	SECTION("generate NACK for missing ordered packet")
	{
		// clang-format off
		std::vector<TestNackGeneratorInput> inputs =
		{
			{ 2381, false,    0, 0, false, 0 },
			{ 2383, false, 2382, 1, false, 1 }
		};
		// clang-format on

		validate(inputs);
	}

	SECTION("sequence wrap generates no NACK")
	{
		// clang-format off
		std::vector<TestNackGeneratorInput> inputs =
		{
			{ 65534, false, 0, 0, false, 0 },
			{ 65535, false, 0, 0, false, 0 },
			{     0, false, 0, 0, false, 0 }
		};
		// clang-format on

		validate(inputs);
	}

	SECTION("generate NACK after sequence wrap")
	{
		// clang-format off
		std::vector<TestNackGeneratorInput> inputs =
		{
			{ 65534, false, 0, 0, false, 0 },
			{ 65535, false, 0, 0, false, 0 },
			{     1, false, 0, 1, false, 1 }
		};
		// clang-format on

		validate(inputs);
	}

	SECTION("generate NACK after sequence wrap, and yet another NACK")
	{
		// clang-format off
		std::vector<TestNackGeneratorInput> inputs =
		{
			{ 65534, false, 0, 0, false,  0 },
			{ 65535, false, 0, 0, false,  0 },
			{     1, false, 0, 1, false,  1 },
			{    11, false, 2, 9, false, 10 },
			{    12,  true, 0, 0, false, 10 },
			{    13,  true, 0, 0, false,  0 }
		};
		// clang-format on

		validate(inputs);
	}

	SECTION("intercalated missing packets")
	{
		// clang-format off
		std::vector<TestNackGeneratorInput> inputs =
		{
			{ 1, false, 0, 0, false, 0 },
			{ 3, false, 2, 1, false, 1 },
			{ 5, false, 4, 1, false, 2 },
			{ 7, false, 6, 1, false, 3 },
			{ 9, false, 8, 1, false, 4 }
		};
		// clang-format on

		validate(inputs);
	}

	SECTION("non contiguous intercalated missing packets")
	{
		// clang-format off
		std::vector<TestNackGeneratorInput> inputs =
		{
			{ 1, false, 0, 0, false, 0 },
			{ 3, false, 2, 1, false, 1 },
			{ 7, false, 4, 3, false, 4 },
			{ 9, false, 8, 1, false, 5 }
		};
		// clang-format on

		validate(inputs);
	}

	SECTION("big jump")
	{
		// clang-format off
		std::vector<TestNackGeneratorInput> inputs =
		{
			{   1, false, 0,   0, false,   0 },
			{ 300, false, 2, 298, false, 298 },
			{   3, false, 0,   0, false, 297 },
			{   4, false, 0,   0, false, 296 },
			{   5, false, 0,   0, false, 295 }
		};
		// clang-format on

		validate(inputs);
	}

	SECTION("Key Frame required. Nack list too large to be requested")
	{
		// clang-format off
		std::vector<TestNackGeneratorInput> inputs =
		{
			{    1, false, 0, 0, false, 0 },
			{ 3000, false, 0, 0,  true, 0 }
		};
		// clang-format on

		validate(inputs);
	}
}
