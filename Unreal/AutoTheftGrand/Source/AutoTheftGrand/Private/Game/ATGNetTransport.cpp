#include "Game/ATGNetTransport.h"
#include "AutoTheftGrand.h"
#include "IWebSocket.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "WebSocketsModule.h"

namespace {
std::string Utf8(const FString& S) { return std::string(TCHAR_TO_UTF8(*S)); }
FString FromUtf8(const std::string& S) { return FString(UTF8_TO_TCHAR(S.c_str())); }
}

FString FATGWsTransport::DefaultUrl() {
	FString Url;
	if (FParse::Value(FCommandLine::Get(), TEXT("ATGServer="), Url) && !Url.IsEmpty()) return Url;
	return TEXT("ws://localhost:8080/mp");
}

FATGWsTransport::FATGWsTransport(const FString& InUrl) : Url(InUrl) {
	kind = "ws";
	label = "local server";
}

FATGWsTransport::~FATGWsTransport() {
	bClosing = true;
	if (Timeout.IsValid()) FTSTicker::GetCoreTicker().RemoveTicker(Timeout);
	if (Retry.IsValid()) FTSTicker::GetCoreTicker().RemoveTicker(Retry);
	Unbind();
	if (Ws.IsValid() && Ws->IsConnected()) Ws->Close();
	Ws.Reset();
}

void FATGWsTransport::Unbind() {
	if (!Ws.IsValid()) return;
	Ws->OnConnected().Clear();
	Ws->OnConnectionError().Clear();
	Ws->OnClosed().Clear();
	Ws->OnMessage().Clear();
}

void FATGWsTransport::Open() {
	if (!FModuleManager::Get().IsModuleLoaded(TEXT("WebSockets"))) FModuleManager::Get().LoadModule(TEXT("WebSockets"));
	Unbind();
	Ws = FWebSocketsModule::Get().CreateWebSocket(Url, TEXT(""));
	Ws->OnConnected().AddLambda([this]() { SendJoin(); });
	Ws->OnConnectionError().AddLambda([this](const FString& Error) {
		UE_LOG(LogATG, Warning, TEXT("Multiplayer: could not reach %s (%s)"), *Url, *Error);
		if (!bJoined) Finish(false, Utf8(FString::Printf(TEXT("Could not reach the multiplayer server at %s. Start it with node server.mjs in the game's folder."), *Url)));
		else if (!bClosing) { if (onStatus) onStatus(false); }
	});
	Ws->OnClosed().AddLambda([this](int32, const FString&, bool) {
		if (bClosing) return;
		if (onStatus) onStatus(false);
		// (try again in two seconds while we're meant to be in a room)
		if (bJoined && !Retry.IsValid())
			Retry = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([this](float) { Retry.Reset(); if (!bClosing) Open(); return false; }), 2.f);
	});
	Ws->OnMessage().AddLambda([this](const FString& Text) { OnMessage(Text); });
	Ws->Connect();
}

void FATGWsTransport::SendJoin() {
	atg::Json M = atg::Json::Object();
	M.set("t", atg::Json::String("join"));
	M.set("room", atg::Json::String(Room.IsEmpty() ? "public" : Utf8(Room)));
	if (Ws.IsValid()) Ws->Send(FromUtf8(M.dump()));
}

void FATGWsTransport::join(const std::string& code, std::function<void(bool, const std::string&)> done) {
	bClosing = false;
	Room = FromUtf8(code);
	Done = std::move(done);
	// (the server must answer within five seconds)
	Timeout = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([this](float) { Timeout.Reset(); if (!bJoined) Finish(false, "The multiplayer server did not answer"); return false; }), 5.f);
	if (Ws.IsValid() && Ws->IsConnected()) SendJoin(); else Open();
}

void FATGWsTransport::Finish(bool bOk, const std::string& Why) {
	if (Timeout.IsValid()) { FTSTicker::GetCoreTicker().RemoveTicker(Timeout); Timeout.Reset(); }
	auto D = std::move(Done);
	Done = nullptr;
	if (D) D(bOk, Why);
}

void FATGWsTransport::OnMessage(const FString& Text) {
	const atg::Json M = atg::Json::parse(Utf8(Text));
	if (!M.isObj()) return;
	const std::string T = atg::JStrAt(M, "t");
	if (T == "hello") {
		Id = atg::JStrAt(M, "id");
		const bool bFirst = !bJoined;
		bJoined = true;
		if (onStatus) onStatus(true);
		if (const atg::Json* Peers = M.get("peers"); Peers && Peers->isArr())
			for (const atg::Json& P : Peers->a) {
				const std::string Pid = atg::JStrAt(P, "id");
				const atg::Json* D = P.get("d");
				if (!D || !D->isObj()) continue;
				if (const atg::Json* G = D->get("g"); G && G->isObj() && onPeer) onPeer(Pid, *G, false);
				if (const atg::Json* N = D->get("n"); N && onWorld) onWorld(Pid, *N);
			}
		if (bFirst) Finish(true, "");
	} else if (T == "full") {
		Finish(false, "That room is full (16 players).");
	} else if (T == "s") {
		const std::string Pid = atg::JStrAt(M, "id");
		const atg::Json* D = M.get("d");
		if (!D || !D->isObj()) return;
		if (const atg::Json* G = D->get("g")) {
			if (G->type == atg::Json::Null) { if (onLeft) onLeft(Pid); }
			else if (G->isObj() && onPeer) onPeer(Pid, *G, false);
		}
		if (const atg::Json* N = D->get("n"); N && onWorld) onWorld(Pid, *N);
	} else if (T == "left") {
		if (onLeft) onLeft(atg::JStrAt(M, "id"));
	}
}

void FATGWsTransport::send(const atg::Json& state) {
	if (!connected()) return;
	atg::Json D = atg::Json::Object();
	D.set("g", state);
	atg::Json M = atg::Json::Object();
	M.set("t", atg::Json::String("s"));
	M.set("d", D);
	Ws->Send(FromUtf8(M.dump()));
}

void FATGWsTransport::sendWorld(const atg::Json& n) {
	if (!connected()) return;
	atg::Json D = atg::Json::Object();
	D.set("n", n);
	atg::Json M = atg::Json::Object();
	M.set("t", atg::Json::String("s"));
	M.set("d", D);
	Ws->Send(FromUtf8(M.dump()));
}

void FATGWsTransport::leave() {
	bClosing = true;
	bJoined = false;
	Room.Reset();
	if (Retry.IsValid()) { FTSTicker::GetCoreTicker().RemoveTicker(Retry); Retry.Reset(); }
	if (Ws.IsValid() && Ws->IsConnected()) {
		atg::Json M = atg::Json::Object();
		M.set("t", atg::Json::String("leave"));
		Ws->Send(FromUtf8(M.dump()));
		Ws->Close();
	}
}

bool FATGWsTransport::connected() const { return Ws.IsValid() && Ws->IsConnected(); }
