#!/bin/bash
# ==============================================================================
# SPACE GL - Enterprise Revision Script (Release++)
# ==============================================================================

set -euo pipefail

GIT_ROOT="../spacegl"
SPEC_FILE="spacegl.spec"
CHANGELOG_FILE="changelog"

# Funzioni di Logging (Formato Standardizzato e ANSI Colors se supportato)
log_info()    { echo -e "[\033[34m ℹ \033[0m] $1"; }
log_success() { echo -e "[\033[32m ✔ \033[0m] $1"; }
log_warn()    { echo -e "[\033[33m ⚠ \033[0m] $1"; }
log_error()   { echo -e "[\033[31m ✘ \033[0m] $1" >&2; }

# Funzione per cleanup e rollback in caso di errore
cleanup_and_rollback() {
    local exit_code=$?
    log_error "Operazione fallita (Exit Code: $exit_code). Avvio rollback..."
    
    # Cleanup eventuali file spazzatura lasciati nei branch tree originali
    pushd "$GIT_ROOT" > /dev/null
        if [ -f "${SPEC_FILE}.bak" ]; then
            mv "${SPEC_FILE}.bak" "$SPEC_FILE" 2>/dev/null || true
        fi
        rm -f ".tmp_changelog"
        
        # Effettuiamo un reset solo se siamo andati avanti rispetto ad 'origin/main' senza fare push
        local unpushed_commits
        unpushed_commits=$(git log origin/main..HEAD --oneline 2>/dev/null | wc -l || echo 0)
        if [ "$unpushed_commits" -gt 0 ]; then
            log_warn "Rilevati $unpushed_commits commit locali non validati. Ripristino a origin/main..."
            git reset --hard origin/main >/dev/null 2>&1 || true
        fi
    popd > /dev/null
    exit "$exit_code"
}
trap cleanup_and_rollback ERR

# Recupero della Versione
VER=$(grep "^Version:" "$SPEC_FILE" | awk '{print $2}')
log_info "Starting Enterprise Revision bump for: $VER"

# 1. Validazione requisiti ed assett
log_info "Validating SPEC and assets..."
if ! command -v rpmlint &>/dev/null; then
    log_warn "Il comando 'rpmlint' non è installato. Validazione SPEC ignorata."
elif ! rpmlint "$SPEC_FILE" &>/dev/null; then
    log_warn "rpmlint ha rilevato potenziali warning in $SPEC_FILE. Procedere comunque? (y/N)"
    read -r -p "> " confirm
    [[ "${confirm,,}" != "y" ]] && exit 1
fi

TODAY_DATE=$(LC_ALL=en_US.UTF-8 date +"%a %b %d %Y")
if ! grep -q "$TODAY_DATE" "$CHANGELOG_FILE"; then
    log_warn "Il Changelog non contiene una voce per oggi ($TODAY_DATE). Continuare? (y/N)"
    read -r -p "> " confirm
    [[ "${confirm,,}" != "y" ]] && exit 1
fi

# 2. Sincronizzazione Workspace tramite Rsync
log_info "Syncing workspace via rsync..."
if ! command -v rsync &>/dev/null; then
    log_error "Comando 'rsync' mancante. Impossibile sincronizzare. Interruzione."
    exit 1
fi

# Sincronizza tutta la cartella eliminando i file assenti, escludendo la directory git e file inutili
rsync -av --exclude='.git' --exclude='build' --delete ./ "$GIT_ROOT/" >/dev/null

# 3. Preparazione, Commit e Generazione SRPM
pushd "$GIT_ROOT" > /dev/null
    git add .
    DESC=$(grep "^-" "changelog" | head -n 1 | sed 's/^- //')
    
    log_info "Committing source updates..."
    if git diff-index --quiet HEAD --; then
        git commit --allow-empty -m "[revision] $VER: $DESC" >/dev/null
    else
        git commit -m "[revision] $VER: $DESC" >/dev/null
    fi
    
    log_info "Building SRPM (Revision increment via rpmautospec)..."
    git archive --format=tar.gz --prefix="spacegl-${VER}/" "HEAD" -o "spacegl-${VER}.tar.gz"
    mv "spacegl-${VER}.tar.gz" ~/rpmbuild/SOURCES/
    
    CALC_REL=$(rpmautospec calculate-release "$SPEC_FILE" | grep "Calculated release number" | sed 's/.*: //')
    log_info "Forcing release $CALC_REL for this SRPM build."
    
    # Appiattimento del file SPEC temporaneamente per assicurarci che l'SRPM autonomo
    # contenga le informazioni di changelog e release costanti, bypassando proxy fallbacks.
    cp "$SPEC_FILE" "${SPEC_FILE}.bak"
    sed -i "s/%autorelease/$CALC_REL%{?dist}/" "$SPEC_FILE"
    rpmautospec generate-changelog > .tmp_changelog
    sed -i '/%autochangelog/r .tmp_changelog' "$SPEC_FILE"
    sed -i '/%autochangelog/d' "$SPEC_FILE"
    
    rpmbuild --quiet -bs "$SPEC_FILE" --define "_sourcedir $HOME/rpmbuild/SOURCES"
    
    # Ripristino dello SPEC purista con macro intatte per Copr SCM
    mv "${SPEC_FILE}.bak" "$SPEC_FILE"
    rm -f .tmp_changelog "$HOME/rpmbuild/SOURCES/spacegl-${VER}.tar.gz"

    # Pusha le revision soloo se l'SRPM locale è stato validato con successo
    log_info "Syncing remote repository..."
    git pull --rebase origin main >/dev/null
    git push origin main >/dev/null
popd > /dev/null

# Disabilita la TRAP
trap - ERR
log_success "Revision for $VER completed successfully."
