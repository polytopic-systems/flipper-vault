/*
 * ZeroFIDO
 * Copyright (C) 2026 Alex Stoyanov
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, version 3 or later.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <https://www.gnu.org/licenses/>.
 */

#pragma once

#include <stdbool.h>

typedef struct ZerofidoApp ZerofidoApp;

/*
 * Notification helpers map authenticator events to LED/vibration/sound
 * patterns through the notification service, so the Flipper's own vibro,
 * sound volume and stealth settings apply. LED: yellow while asking, blue
 * wink, green approved, red denied/error.
 */
bool zerofido_notify_init(ZerofidoApp *app);
void zerofido_notify_deinit(ZerofidoApp *app);
void zerofido_notify_prompt(ZerofidoApp *app);
void zerofido_notify_wink(ZerofidoApp *app);
void zerofido_notify_success(ZerofidoApp *app);
void zerofido_notify_error(ZerofidoApp *app);
void zerofido_notify_reset(ZerofidoApp *app);
/* Vault cues: a light detent as the list/dial moves, and the unlock chime. */
void zerofido_notify_tick(ZerofidoApp *app);
void zerofido_notify_unlock(ZerofidoApp *app);
