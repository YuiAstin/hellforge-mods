# The Stalker's cloak on a Helldiver (research, 2026-11-05)

Idea: an invisibility ability that wears the Terminid Stalker's shimmer. The look only; making enemies lose track of
her is the stealth logic (Lost Contact's break contact), and by the user's rule a backpack or support weapon stays
visible and turns the functional part off.

- game.dll has no cloak / stealth / refraction strings: the cloak is material data (hashed names), switched by the
  Stalker's own material settings.
- Plan: HD2Runtime's engine API on the HUD side: find the Stalker's unit, read its meshes' materials, put them on her
  body meshes (Mesh.set_material per slot), put hers back after. Its package only loads on Terminid missions: a kit
  would load it itself (the Stalker's entity resource 0xF540CA9D9D4A422E, HD2Runtime's enemy catalogue).
- If the Stalker's texture shows through: a smoke puff when it switches on and an instant fade hide it (user's idea).
- Only she sees it (local material change); teammates see her normally.

Cloak Probe 0.1.0 (probes/cloak-probe): F12 logs the engine's mesh / material calls and the nearest Stalker's and her
meshes and materials; Ctrl+F12 copies the Stalker's materials onto her (smoke puff first: Eagle Smoke's round,
projectile 16); Shift+F12 restores. Also logs every explosion within 15 m (throw a G-3 Smoke to learn its row).
Results: (pending a Terminid mission).
