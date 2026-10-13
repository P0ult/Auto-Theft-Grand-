// The multiplayer relay built into server.mjs, from Unreal (transports.js WsTransport): a WebSocket to
// ws://host:port/mp. {t:'join', room} gets {t:'hello', id, peers}; each player's state goes out as
// {t:'s', d:{g: state}} and comes in as {t:'s', id, d}; {t:'left', id} when someone goes. A browser running
// the game against the same server shares the room.
#pragma once

#include "CoreMinimal.h"
#include "Containers/Ticker.h"
#include "Sim/Net.h"

class IWebSocket;

class FATGWsTransport : public atg::NetTransport {
public:
	explicit FATGWsTransport(const FString& InUrl);
	virtual ~FATGWsTransport() override;

	virtual void join(const std::string& code, std::function<void(bool, const std::string&)> done) override;
	virtual void send(const atg::Json& state) override;
	virtual void sendWorld(const atg::Json& n) override;
	virtual void leave() override;
	virtual bool connected() const override;
	virtual std::string selfId() const override { return Id; }

	// the relay's address: -ATGServer=ws://host:port/mp on the command line, else the local one
	static FString DefaultUrl();

private:
	FString Url;
	TSharedPtr<IWebSocket> Ws;
	std::string Id;
	FString Room;
	bool bClosing = false, bJoined = false;
	std::function<void(bool, const std::string&)> Done;
	FTSTicker::FDelegateHandle Timeout, Retry;

	void Open();
	void SendJoin();
	void OnMessage(const FString& Text);
	void Finish(bool bOk, const std::string& Why);
	void Unbind();
};
