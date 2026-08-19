// Copyright Perforce Software, Inc. All Rights Reserved.

#pragma once

#if WITH_AUTOMATION_TESTS

#include "CoreMinimal.h"
#include "P4SearchHttp.h"
#include "Interfaces/IHttpRequest.h"
#include "Interfaces/IHttpResponse.h"

// Minimal in-memory stand-in for IHttpResponse — just enough of IHttpBase/IHttpResponse for the
// fields P4Search.cpp actually reads (response code, body, and the one header it reads back,
// X-Nonce-Token on login).
class FP4SearchFakeHttpResponse : public IHttpResponse
{
public:
	FP4SearchFakeHttpResponse(int32 InResponseCode, FString InBody, TMap<FString, FString> InHeaders = {})
		: ResponseCode(InResponseCode)
		, Body(MoveTemp(InBody))
		, Headers(MoveTemp(InHeaders))
	{
		FTCHARToUTF8 Converter(*Body);
		Content.Append(reinterpret_cast<const uint8*>(Converter.Get()), Converter.Length());
	}

	virtual int32 GetResponseCode() const override { return ResponseCode; }
	virtual FString GetContentAsString() const override { return Body; }

	virtual FString GetURL() const override { return FString(); }
	virtual const FString& GetEffectiveURL() const override { static const FString Empty; return Empty; }
	virtual EHttpRequestStatus::Type GetStatus() const override { return EHttpRequestStatus::Succeeded; }
	virtual EHttpFailureReason GetFailureReason() const override { return EHttpFailureReason::None; }
	virtual FString GetURLParameter(const FString&) const override { return FString(); }
	virtual FString GetHeader(const FString& HeaderName) const override
	{
		const FString* Found = Headers.Find(HeaderName);
		return Found ? *Found : FString();
	}
	virtual TArray<FString> GetAllHeaders() const override
	{
		TArray<FString> Result;
		for (const TPair<FString, FString>& Pair : Headers)
		{
			Result.Add(Pair.Key + TEXT(": ") + Pair.Value);
		}
		return Result;
	}
	virtual FString GetContentType() const override { return GetHeader(TEXT("Content-Type")); }
	virtual uint64 GetContentLength() const override { return static_cast<uint64>(Content.Num()); }
	virtual const TArray<uint8>& GetContent() const override { return Content; }

private:
	int32 ResponseCode;
	FString Body;
	TMap<FString, FString> Headers;
	TArray<uint8> Content;
};

// Minimal in-memory stand-in for IHttpRequest. Captures whatever the code under test sets
// (URL/verb/headers/body) for assertions, and ProcessRequest() completes synchronously with a
// pre-set canned response — no real network I/O, no waiting a tick for the delegate to fire.
class FP4SearchFakeHttpRequest : public IHttpRequest
{
public:
	TSharedPtr<FP4SearchFakeHttpResponse, ESPMode::ThreadSafe> CannedResponse;
	bool bCannedSuccess = true;

	FString CapturedURL;
	FString CapturedVerb = TEXT("GET");
	TMap<FString, FString> CapturedHeaders;
	FString CapturedBodyString;
	TArray<uint8> CapturedBodyBytes;

	virtual FString GetURL() const override { return CapturedURL; }
	virtual const FString& GetEffectiveURL() const override { return CapturedURL; }
	virtual EHttpRequestStatus::Type GetStatus() const override
	{
		return bProcessed ? EHttpRequestStatus::Succeeded : EHttpRequestStatus::NotStarted;
	}
	virtual EHttpFailureReason GetFailureReason() const override { return EHttpFailureReason::None; }
	virtual FString GetURLParameter(const FString&) const override { return FString(); }
	virtual FString GetHeader(const FString& HeaderName) const override
	{
		const FString* Found = CapturedHeaders.Find(HeaderName);
		return Found ? *Found : FString();
	}
	virtual TArray<FString> GetAllHeaders() const override
	{
		TArray<FString> Result;
		for (const TPair<FString, FString>& Pair : CapturedHeaders)
		{
			Result.Add(Pair.Key + TEXT(": ") + Pair.Value);
		}
		return Result;
	}
	virtual FString GetContentType() const override { return GetHeader(TEXT("Content-Type")); }
	virtual uint64 GetContentLength() const override { return static_cast<uint64>(CapturedBodyBytes.Num()); }
	virtual const TArray<uint8>& GetContent() const override { return CapturedBodyBytes; }

	virtual FString GetVerb() const override { return CapturedVerb; }
	virtual void SetVerb(const FString& Verb) override { CapturedVerb = Verb; }
	virtual void SetURL(const FString& URL) override { CapturedURL = URL; }
	virtual FString GetOption(const FName) const override { return FString(); }
	virtual void SetOption(const FName, const FString&) override {}
	virtual void SetContent(const TArray<uint8>& ContentPayload) override { CapturedBodyBytes = ContentPayload; }
	virtual void SetContent(TArray<uint8>&& ContentPayload) override { CapturedBodyBytes = MoveTemp(ContentPayload); }
	virtual void SetContentAsString(const FString& ContentString) override { CapturedBodyString = ContentString; }
	virtual bool SetContentAsStreamedFile(const FString&) override { return false; }
	virtual bool SetContentFromStream(TSharedRef<FArchive, ESPMode::ThreadSafe>) override { return false; }
	virtual bool SetResponseBodyReceiveStream(TSharedRef<FArchive>) override { return false; }
	virtual void SetHeader(const FString& HeaderName, const FString& HeaderValue) override
	{
		CapturedHeaders.Add(HeaderName, HeaderValue);
	}
	virtual void AppendToHeader(const FString& HeaderName, const FString& AdditionalHeaderValue) override
	{
		FString& Existing = CapturedHeaders.FindOrAdd(HeaderName);
		Existing = Existing.IsEmpty() ? AdditionalHeaderValue : Existing + TEXT(",") + AdditionalHeaderValue;
	}
	virtual void SetTimeout(float) override {}
	virtual void SetActivityTimeout(float) override {}
	virtual void ClearTimeout() override {}
	virtual void ResetTimeoutStatus() override {}
	virtual TOptional<float> GetTimeout() const override { return TOptional<float>(); }

	virtual bool ProcessRequest() override
	{
		bProcessed = true;
		CompleteDelegate.ExecuteIfBound(AsShared(), CannedResponse, bCannedSuccess);
		return true;
	}
	virtual FHttpRequestCompleteDelegate& OnProcessRequestComplete() override { return CompleteDelegate; }
	virtual FHttpRequestProgressDelegate64& OnRequestProgress64() override { return ProgressDelegate; }
	virtual FHttpRequestWillRetryDelegate& OnRequestWillRetry() override { return WillRetryDelegate; }
	virtual FHttpRequestHeaderReceivedDelegate& OnHeaderReceived() override { return HeaderReceivedDelegate; }
	virtual FHttpRequestStatusCodeReceivedDelegate& OnStatusCodeReceived() override { return StatusCodeReceivedDelegate; }
	virtual void CancelRequest() override {}
	virtual const FHttpResponsePtr GetResponse() const override { return CannedResponse; }
	virtual void Tick(float) override {}
	virtual float GetElapsedTime() const override { return 0.0f; }
	virtual void SetDelegateThreadPolicy(EHttpRequestDelegateThreadPolicy) override {}
	virtual EHttpRequestDelegateThreadPolicy GetDelegateThreadPolicy() const override
	{
		return EHttpRequestDelegateThreadPolicy::CompleteOnGameThread;
	}
	virtual void ProcessRequestUntilComplete() override { ProcessRequest(); }

private:
	bool bProcessed = false;
	FHttpRequestCompleteDelegate CompleteDelegate;
	FHttpRequestProgressDelegate64 ProgressDelegate;
	FHttpRequestWillRetryDelegate WillRetryDelegate;
	FHttpRequestHeaderReceivedDelegate HeaderReceivedDelegate;
	FHttpRequestStatusCodeReceivedDelegate StatusCodeReceivedDelegate;
};

// Test double for IP4SearchHttpRequester. Responses are queued FIFO — CreateRequest() hands
// each new fake request the next queued response, so a test can line up a sequence for chained
// requests (e.g. FetchRibbon's meta request then its image request). If the queue runs dry, a
// default 200/empty-JSON response is handed back so an unexpected extra request (e.g. the
// ExecuteSearch a successful PerformLogin chains into) doesn't leave a request's delegate
// waiting on a null response.
class FP4SearchFakeHttpRequester : public IP4SearchHttpRequester
{
public:
	TArray<TSharedPtr<FP4SearchFakeHttpRequest, ESPMode::ThreadSafe>> CreatedRequests;

	void QueueResponse(int32 ResponseCode, const FString& Body, TMap<FString, FString> Headers = {}, bool bSuccess = true)
	{
		QueuedResponses.Add(MakeShared<FP4SearchFakeHttpResponse, ESPMode::ThreadSafe>(ResponseCode, Body, MoveTemp(Headers)));
		QueuedSuccessFlags.Add(bSuccess);
	}

	virtual FHttpRequestRef CreateRequest() override
	{
		TSharedRef<FP4SearchFakeHttpRequest, ESPMode::ThreadSafe> FakeRequest =
			MakeShared<FP4SearchFakeHttpRequest, ESPMode::ThreadSafe>();

		if (QueuedResponses.Num() > 0)
		{
			FakeRequest->CannedResponse = QueuedResponses[0];
			FakeRequest->bCannedSuccess = QueuedSuccessFlags[0];
			QueuedResponses.RemoveAt(0);
			QueuedSuccessFlags.RemoveAt(0);
		}
		else
		{
			FakeRequest->CannedResponse = MakeShared<FP4SearchFakeHttpResponse, ESPMode::ThreadSafe>(200, TEXT("{}"));
			FakeRequest->bCannedSuccess = true;
		}

		CreatedRequests.Add(FakeRequest);
		return FakeRequest;
	}

private:
	TArray<TSharedPtr<FP4SearchFakeHttpResponse, ESPMode::ThreadSafe>> QueuedResponses;
	TArray<bool> QueuedSuccessFlags;
};

#endif // WITH_AUTOMATION_TESTS
